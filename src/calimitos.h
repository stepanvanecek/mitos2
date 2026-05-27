/**
 * calimitos.h
 *
 * Mitos (re-)implementation using Caliper as a backend.
 *
 * Configuration is built on top of Caliper's ConfigManager API: a custom
 * "mitos-sampler" config spec exposes the sampling backend (PEBS / IBS Op
 * / IBS Fetch) and an MPI toggle as proper Caliper Options. Callers can
 * either use the legacy Mitos_set_* setters or pass a Caliper option
 * string directly to Mitos_begin_sampler ("backend=ibs_op,mpi,sample.period=64").
 *
 * Output layout (mitos/MemAxes-compatible):
 *
 *   calimitos_<unix_ts>/
 *       data/
 *           trace.cali       (raw Caliper recorder output)
 *           samples.csv      (MemAxes-format CSV produced by cali_to_csv.py)
 *       src/<copied source files>
 *       hwdata/              (reserved)
 *       hardware.xml         (hwloc topology dump)
 *
 * Call ordering:
 *
 *   1. (optional) configure: Mitos_set_backend, Mitos_set_sample_*,
 *      Mitos_set_recorder_*, Mitos_enable_mpi.
 *   2. (optional) pre-create any Caliper Attributes the user callback will
 *      append, via Caliper::create_attribute(). Attribute creation is not
 *      async-signal-safe and must NOT happen inside the callback or while
 *      sampling is active.
 *   3. (optional) Mitos_set_handler_fn(fn, args). Must be called before
 *      Mitos_begin_sampler so the callback is in place when the first
 *      sample is delivered.
 *   4. Mitos_begin_sampler([opts]).
 *   5. ... region of interest ...
 *   6. Mitos_end_sampler().
 *
 * Marius Albrecht, 2026, for my Bachelor's thesis
 * "A Future-proof and Microarchitecture-agnostic Approach to Collecting CPU Hardware Samples"
 */

#pragma once

#include <caliper/cali.h>
#include <caliper/Caliper.h>
#include <caliper/ChannelController.h>
#include <caliper/ConfigManager.h>
#include <caliper/sample_callback.h>

#include <hwloc.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace calimitos_detail {

namespace fs = std::filesystem;

// types, etc //

typedef enum {
    backend_auto,
    backend_pebs,
    backend_ibs_op,
    backend_ibs_fetch
} backend_t;

// Mitos_begin_sampler folds these into the Caliper option string before
// appending the caller's optional opts argument (caller wins on conflict).
struct calimitos_overrides
{
    backend_t   backend             = backend_auto; // which sampling backend to use
    bool        backend_set         = false;
    bool        mpi_enabled         = false; // whether to pull in mpi relatec caliper config
    bool        mpi_set             = false;
    int64_t     sample_period       = 0;
    bool        sample_period_set   = false;
    int64_t     latency_threshold   = 0;
    bool        latency_set         = false;
    std::string recorder_filename;
    std::string recorder_directory;
    bool        output_set          = false;
};

// state // (`inline` => single shared instance across TUs)

inline std::unique_ptr<cali::ConfigManager> g_mgr;
inline bool is_sampler_running = false;
inline calimitos_overrides g_overrides;
inline std::string g_result_dir; // used for output and source copying
inline std::string g_resolved_recorder_path; // used for recorder config and as cali_to_csv.py output path
// Path to the cali_to_csv.py script.
// env var CALIMITOS_CALI_TO_CSV; on PATH, the CWD or set by Mitos_set_cali_to_csv_script()
inline std::string g_cali_to_csv_script = []() -> std::string {
    const char* e = std::getenv("CALIMITOS_CALI_TO_CSV");
    return e ? std::string(e) : std::string("cali_to_csv.py");
}();

inline const char* mitos_sampler_spec = R"json(
{
 "name"        : "mitos-sampler",
 "description" : "Mitos-style memory-access sampling via libpfm + recorder",
 "services"    : [ "libpfm", "sample_callback", "symbollookup", "pthread", "recorder", "trace" ],
 "categories"  : [ "output" ],
 "config"      :
 {
  "CALI_CHANNEL_FLUSH_ON_EXIT":         "false",
  "CALI_LIBPFM_SAMPLE_PERIOD":          "4000",
  "CALI_LIBPFM_PRECISE_IP":             "2",
  "CALI_LIBPFM_BUFFER_SIZE_PAGES":      "64",
  "CALI_SYMBOLLOOKUP_LOOKUP_FUNCTIONS": "true",
  "CALI_SYMBOLLOOKUP_LOOKUP_FILE":      "true",
  "CALI_SYMBOLLOOKUP_LOOKUP_LINE":      "true",
  "CALI_SYMBOLLOOKUP_LOOKUP_SOURCELOC": "true",
  "CALI_SYMBOLLOOKUP_LOOKUP_MODULE":    "false",
  "CALI_RECORDER_DIRECTORY":            ".",
  "CALI_RECORDER_FILENAME":             "trace.cali",
  "CALI_SAMPLE_CALLBACK_FILTER_ATTRIBUTE": "libpfm.event_sample_name"
 },
 "defaults"    : { "backend": "auto" },
 "options":
 [
  {
   "name": "backend",
   "type": "string",
   "description": "Sampling backend: 'auto' (default), 'pebs', 'ibs_op', 'ibs_fetch'"
  },{
   "name": "mpi",
   "type": "bool",
   "description": "Enable mpi + mpireport services; tag samples with mpi.rank and emit a cross-rank .cali artifact",
   "services": [ "mpi", "mpireport" ],
   "config":
   {
    "CALI_RECORDER_FILENAME":           "trace_rank%mpi.rank%.cali",
    "CALI_MPIREPORT_FILENAME":          "mpireport.cali",
    "CALI_MPIREPORT_WRITE_ON_FINALIZE": "true",
    "CALI_MPIREPORT_CONFIG":            "select * format cali"
   }
  },{
   "name": "sample.period",
   "type": "int",
   "description": "libpfm SAMPLE_PERIOD: trigger one sample every N events"
  },{
   "name": "latency.threshold",
   "type": "int",
   "description": "PEBS latency threshold (CALI_LIBPFM_CONFIG1); ignored on IBS"
  },{
   "name": "output",
   "type": "string",
   "description": "Recorder output path. May contain Caliper attribute templates like %mpi.rank%."
  }
 ]
}
)json";

inline backend_t detect_backend_from_cpuinfo()
{
    std::ifstream file("/proc/cpuinfo");
    if (!file.is_open()) {
        std::cerr << "calimitos: could not open /proc/cpuinfo to auto-detect PEBS/IBS; defaulting to PEBS." << std::endl;
        return backend_pebs;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("flags", 0) == 0) {
            if (line.find("pebs") != std::string::npos)
                return backend_pebs;
            if (line.find("ibs") != std::string::npos)
                return backend_ibs_op;
            std::cerr << "calimitos: neither pebs nor ibs in /proc/cpuinfo flags; defaulting to PEBS." << std::endl;
            return backend_pebs;
        }
    }
    return backend_pebs;
}

inline const char* backend_to_str(backend_t b)
{
    switch (b) {
        case backend_pebs:      return "pebs";
        case backend_ibs_op:    return "ibs_op";
        case backend_ibs_fetch: return "ibs_fetch";
        default:                return "auto";
    }
}

// Applies the options that can't be expressed declaratively in the JSON spec
class MitosSamplerController : public cali::ChannelController
{
public:
    MitosSamplerController(const char*                         name,
                           const cali::config_map_t&           initial_cfg,
                           const cali::ConfigManager::Options& opts)
        : cali::ChannelController(name, 0, initial_cfg)
    {
        backend_t backend = backend_pebs;

        // resolve backend
        const std::string backend_str = opts.get("backend", "auto");
        if      (backend_str == "auto")      backend = detect_backend_from_cpuinfo();
        else if (backend_str == "pebs")      backend = backend_pebs;
        else if (backend_str == "ibs_op")    backend = backend_ibs_op;
        else if (backend_str == "ibs_fetch") backend = backend_ibs_fetch;
        else {
            std::cerr << "calimitos: weird backend \"" << backend_str << "\"; defaulting to PEBS." << std::endl;
            backend = backend_pebs;
        }

        // adjust config based on backend
        if (backend == backend_pebs) {
            config()["CALI_LIBPFM_EVENTS"]            = "MEM_TRANS_RETIRED:LOAD_LATENCY";
            // Default matches Mitos library default (procsmpl.cpp: sample_latency_threshold = 8)
            config()["CALI_LIBPFM_CONFIG1"]           = opts.get("latency.threshold", "8");
            config()["CALI_LIBPFM_SAMPLE_ATTRIBUTES"] = "ip,id,time,tid,period,cpu,addr,weight,data_src";
        } else if (backend == backend_ibs_op) {
            config()["CALI_LIBPFM_EVENTS"]            = "amd64_ibs_op::IBS_OP_ALL";
            // sample every n cycles; set bit 19 to sample every n ops instead
            config()["CALI_LIBPFM_CONFIG"]            = "0";
            config()["CALI_LIBPFM_SAMPLE_ATTRIBUTES"] = "ip,id,time,tid,period,cpu,addr,weight,raw";
            if (opts.is_set("latency.threshold"))
                std::cerr << "calimitos: latency.threshold is PEBS-only and is ignored on IBS Op." << std::endl;
        } else if (backend == backend_ibs_fetch) {
            config()["CALI_LIBPFM_EVENTS"]            = "amd64_ibs_fetch::IBS_FETCH_ALL";
            // bit 57 (ibs_rand_en) randomizes the fetch counter; matches Mitos USE_IBS_FETCH default
            config()["CALI_LIBPFM_CONFIG"]            = std::to_string(1ULL << 57);
            config()["CALI_LIBPFM_SAMPLE_ATTRIBUTES"] = "ip,id,time,tid,period,cpu,addr,weight,raw";
            if (opts.is_set("latency.threshold"))
                std::cerr << "calimitos: latency.threshold is PEBS-only and is ignored on IBS Fetch." << std::endl;
        }

        if (opts.is_set("sample.period"))
            config()["CALI_LIBPFM_SAMPLE_PERIOD"] = opts.get("sample.period", "");

        // Apply option specs: pulls in mpi/mpireport services and their
        // CALI_MPIREPORT_* + RECORDER_FILENAME defaults when mpi is enabled.
        opts.update_channel_config(config());
        opts.update_channel_metadata(metadata());

        // Output path runs LAST so a user-supplied path overrides the
        // trace_rank%mpi.rank%.cali default that the mpi option spec sets.
        if (opts.is_set("output")) {
            const std::string out = opts.get("output", "");
            const auto pos = out.find_last_of('/');
            if (pos == std::string::npos) {
                config()["CALI_RECORDER_FILENAME"] = out;
            } else {
                config()["CALI_RECORDER_DIRECTORY"] = out.substr(0, pos);
                config()["CALI_RECORDER_FILENAME"]  = out.substr(pos + 1);
            }
        }

        // used in Mitos_end_sampler
        g_resolved_recorder_path = config()["CALI_RECORDER_DIRECTORY"] + "/" + config()["CALI_RECORDER_FILENAME"];
    }
};

// for cali::ConfigManager::ConfigInfo
inline std::string mitos_check_args(const cali::ConfigManager::Options& opts)
{
    const std::string           backend          = opts.get("backend", "auto");
    const std::set<std::string> allowed_backends = { "auto", "pebs", "ibs_op", "ibs_fetch" };
    if (allowed_backends.find(backend) == allowed_backends.end())
        return std::string("mitos-sampler: invalid backend \"") + backend + "\" (expected auto, pebs, ibs_op, or ibs_fetch)";
    return "";
}

// for cali::ConfigManager::ConfigInfo
inline cali::ChannelController* mitos_make_controller(const char*                         name,
                                                      const cali::config_map_t&           initial_cfg,
                                                      const cali::ConfigManager::Options& opts)
{
    return new MitosSamplerController(name, initial_cfg, opts);
}

// needed for add_config_spec
inline const cali::ConfigManager::ConfigInfo mitos_sampler_info {
    mitos_sampler_spec, mitos_make_controller, mitos_check_args
};

inline std::string make_result_dir_name()
{
    const auto now = std::chrono::system_clock::now();
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return "calimitos_" + std::to_string(sec);
}

inline int dump_hwloc_xml(const std::string& path)
{
    hwloc_topology_t topo;
    if (hwloc_topology_init(&topo) != 0) {
        std::cerr << "calimitos: hwloc_topology_init failed." << std::endl;
        return 1;
    }
    if (hwloc_topology_load(topo) != 0) {
        std::cerr << "calimitos: hwloc_topology_load failed." << std::endl;
        hwloc_topology_destroy(topo);
        return 1;
    }
    int rc = hwloc_topology_export_xml(topo, path.c_str(), 0);
    hwloc_topology_destroy(topo);
    if (rc != 0)
        std::cerr << "calimitos: hwloc_topology_export_xml failed." << std::endl;
    return rc;
}

// Run cali_to_csv.py with --memaxes to produce samples.csv and an optional
// source-file list. sources_file may be empty to skip --sources-out.
// Returns the script's exit code (0 on success).
inline int run_cali_to_csv(const std::string& cali_file,
                           const std::string& csv_file,
                           const std::string& sources_file = "")
{
    std::string cmd = "python3 \"" + g_cali_to_csv_script + "\" --memaxes \""
                      + cali_file + "\" \"" + csv_file + "\"";
    if (!sources_file.empty())
        cmd += " --sources-out \"" + sources_file + "\"";
    const int ret = std::system(cmd.c_str());
    if (ret != 0)
        std::cerr << "calimitos: cali_to_csv.py exited with code " << ret
                  << " (cmd: " << cmd << ")" << std::endl;
    return ret;
}

// Read the source-file list written by --sources-out (one absolute path per
// line) and copy each existing non-system file into src_dir.
inline void copy_source_files_from_list(const std::string& list_path,
                                        const std::string& src_dir)
{
    std::ifstream f(list_path);
    if (!f) {
        std::cerr << "calimitos: could not open source list " << list_path << std::endl;
        return;
    }

    int copied = 0, skipped_system = 0;
    std::string src;
    while (std::getline(f, src)) {
        if (src.empty()) continue;
        if (src.rfind("/usr", 0) == 0) { ++skipped_system; continue; }
        std::error_code ec;
        if (!fs::exists(src, ec)) continue;
        const fs::path dst = fs::path(src_dir) / fs::path(src).filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (!ec) ++copied;
    }
    std::cerr << "calimitos: copied " << copied << " source file(s) to " << src_dir;
    if (skipped_system > 0)
        std::cerr << " (skipped " << skipped_system << " system file(s) under /usr)";
    std::cerr << std::endl;
}

inline void append_opt(std::string& s, const std::string& entry)
{
    if (entry.empty()) return;
    if (!s.empty()) s.push_back(',');
    s.append(entry);
}

// public API. Defined inside calimitos_detail to keep unqualified
// lookup of state above; re-exported at global scope below.

inline void Mitos_set_sample_event_period(uint64_t p) {
    g_overrides.sample_period = static_cast<int64_t>(p);
    g_overrides.sample_period_set = true;
}

inline void Mitos_set_sample_latency_threshold(uint64_t t) {
    g_overrides.latency_threshold = static_cast<int64_t>(t);
    g_overrides.latency_set = true;
}

// Preconditions:
//   - Must be called before Mitos_begin_sampler. Calling it after sampling
//     has started leaves the initial samples flowing through the default
//     no-op dispatcher and is otherwise unsupported by the underlying
//     sample_callback service.
//   - Any Caliper Attributes that `fn` will pass to SnapshotBuilder::append
//     must already have been created (via Caliper::create_attribute()).
//     Attribute creation is not async-signal-safe and must not occur inside
//     `fn`, which runs in signal context.
inline void Mitos_set_handler_fn(cali::sample_callback::callback_fn fn, void *args) {
    cali::sample_callback::register_callback(fn, args);
}

inline void Mitos_set_recorder_filename(const std::string& filename) {
    g_overrides.recorder_filename = filename;
    g_overrides.output_set = true;
}

inline void Mitos_set_recorder_directory(const std::string& directory) {
    g_overrides.recorder_directory = directory;
    g_overrides.output_set = true;
}

inline void Mitos_set_backend(const backend_t backend) {
    g_overrides.backend = backend;
    g_overrides.backend_set = true;
}

inline void Mitos_enable_mpi(bool enable = true) {
    g_overrides.mpi_enabled = enable;
    g_overrides.mpi_set = true;
}

inline void Mitos_set_cali_to_csv_script(const std::string& path) {
    g_cali_to_csv_script = path;
}

// `opts` is an optional Caliper option string (e.g. "backend=ibs_op,mpi") that
// is appended AFTER the setter-derived options so it wins on conflict.
inline int Mitos_begin_sampler(const std::string& opts = "")
{
    if (is_sampler_running) {
        std::cerr << "calimitos: Mitos_begin_sampler called while already is_sampler_running." << std::endl;
        return -1;
    }

    // Build the mitos-style result directory. Caller-supplied output path
    // (via Mitos_set_recorder_directory or opts="output=...") wins.
    if (!g_overrides.output_set && opts.find("output=") == std::string::npos) {
        g_result_dir = make_result_dir_name();
        std::error_code ec;
        fs::create_directory(g_result_dir, ec);
        fs::create_directory(g_result_dir + "/data", ec);
        fs::create_directory(g_result_dir + "/src", ec);
        fs::create_directory(g_result_dir + "/hwdata", ec);

        g_overrides.recorder_directory = g_result_dir + "/data";
        if (g_overrides.recorder_filename.empty())
            g_overrides.recorder_filename = "trace.cali";
        g_overrides.output_set = true;
    } else {
        g_result_dir.clear();
    }

    // construct option string
    //  1. setter-derived overrides
    //  2. append the caller's opts
    std::string opt_list;
    if (g_overrides.backend_set)
        append_opt(opt_list, std::string("backend=") + backend_to_str(g_overrides.backend));
    if (g_overrides.mpi_set)
        append_opt(opt_list, g_overrides.mpi_enabled ? "mpi=true" : "mpi=false");
    if (g_overrides.sample_period_set)
        append_opt(opt_list, "sample.period=" + std::to_string(g_overrides.sample_period));
    if (g_overrides.latency_set)
        append_opt(opt_list, "latency.threshold=" + std::to_string(g_overrides.latency_threshold));
    if (g_overrides.output_set) {
        const std::string dir  = g_overrides.recorder_directory.empty() ? "." : g_overrides.recorder_directory;
        const std::string file = g_overrides.recorder_filename.empty() ? "trace.cali"
                                                                       : g_overrides.recorder_filename;
        append_opt(opt_list, "output=" + dir + "/" + file);
    }
    if (!opts.empty())
        append_opt(opt_list, opts);

    std::string cs = "mitos-sampler";
    if (!opt_list.empty())
        cs += "(" + opt_list + ")";

    g_mgr = std::make_unique<cali::ConfigManager>();
    g_mgr->add_config_spec(mitos_sampler_info);
    g_mgr->add(cs.c_str());
    if (g_mgr->error()) {
        std::cerr << "calimitos: ConfigManager error: " << g_mgr->error_msg() << std::endl;
        g_mgr.reset();
        return -1;
    }

    g_mgr->start();
    is_sampler_running = true;
    return 0;
}

inline void Mitos_end_sampler()
{
    if (!is_sampler_running || !g_mgr) {
        std::cerr << "calimitos: Mitos_end_sampler called but sampler is not is_sampler_running." << std::endl;
        return;
    }

    g_mgr->stop();   // disable sampling before flushing so no new samples arrive
    g_mgr->flush();
    g_mgr.reset();
    is_sampler_running = false;

    // hwloc topology: dump regardless of whether the recorder path is templated.
    if (!g_result_dir.empty())
        dump_hwloc_xml(g_result_dir + "/hardware.xml");

    if (g_resolved_recorder_path.find('%') != std::string::npos) {
        std::cerr << "calimitos: recorder path has unresolved attribute template ("
                  << g_resolved_recorder_path
                  << "); skipping automatic CSV conversion." << std::endl;
        return;
    }

    const bool have_result_dir = !g_result_dir.empty();
    const std::string csv_path = have_result_dir
        ? g_result_dir + "/data/samples.csv"
        : [&]() {
            std::string p = g_resolved_recorder_path;
            const auto dot = p.rfind(".cali");
            if (dot != std::string::npos) p.replace(dot, 5, ".csv"); else p += ".csv";
            return p;
          }();

    const std::string sources_path = have_result_dir
        ? g_result_dir + "/data/source_files.txt"
        : "";

    if (run_cali_to_csv(g_resolved_recorder_path, csv_path, sources_path) == 0
        && have_result_dir) {
        copy_source_files_from_list(sources_path, g_result_dir + "/src");
    }
}

} // namespace calimitos_detail

// Expose only the public API at global scope.
using calimitos_detail::backend_t;
using calimitos_detail::backend_auto;
using calimitos_detail::backend_pebs;
using calimitos_detail::backend_ibs_op;
using calimitos_detail::backend_ibs_fetch;

using calimitos_detail::Mitos_set_sample_event_period;
using calimitos_detail::Mitos_set_sample_latency_threshold;
using calimitos_detail::Mitos_set_handler_fn;
using calimitos_detail::Mitos_set_recorder_filename;
using calimitos_detail::Mitos_set_recorder_directory;
using calimitos_detail::Mitos_set_backend;
using calimitos_detail::Mitos_enable_mpi;
using calimitos_detail::Mitos_set_cali_to_csv_script;
using calimitos_detail::Mitos_begin_sampler;
using calimitos_detail::Mitos_end_sampler;
