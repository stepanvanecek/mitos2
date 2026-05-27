"""
cali_to_csv.py

Convert a Caliper .cali file into a CSV. The default output preserves the raw
Caliper attribute names (e.g. "source.file#libpfm.ip"); --memaxes remaps the
columns to the mitos samples.csv layout that MemAxes consumes.

This needs the caliperreader module from Caliper's Python bindings. Either
install using pip or place the the `caliperreader/` directory from
Caliper/python/caliper-reader/ alongside this script. Python prepends the
script's directory to sys.path automatically, so no venv or pip install is
needed. The os.path.insert below only matters when this file is invoked via
`python -m` from elsewhere.
"""

import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import caliperreader as cr

MEMAXES_COLUMNS = [
    "source", "line", "instruction", "bytes", "virt_addr_offset",
    "ip", "variable", "buffer_size", "dims", "xidx", "yidx", "zidx",
    "pid", "tid", "time", "addr", "cpu", "latency",
    "level", "hit_type", "op_type", "snoop_mode", "tlb_access", "numa",
]

# Caliper attribute -> mitos/MemAxes column.
# Unmapped MemAxes columns are emitted as "??" (mitos's own sentinel for
# missing fields, used when Dyninst or NUMA info isn't available).
# virt_addr_offset is hard-coded to 0 because Caliper hands us absolute
# IPs already resolved by symbollookup.
MEMAXES_FROM_CALI = {
    "source.file#libpfm.ip":     "source",
    "source.line#libpfm.ip":     "line",
    "source.function#libpfm.ip": "instruction",
    "libpfm.ip":                 "ip",
    "alloc.label#libpfm.addr":   "variable",
    "libpfm.tid":                "tid",
    "libpfm.time":               "time",
    "libpfm.addr":               "addr",
    "libpfm.cpu":                "cpu",
    "libpfm.weight":             "latency",
    "libpfm.memory_level":       "level",
    "libpfm.hit_type":           "hit_type",
    "libpfm.operation":          "op_type",
    "libpfm.snoop":              "snoop_mode",
    "libpfm.tlb":                "tlb_access",
    "alloc.index#libpfm.addr":   "xidx",
    "mpi.rank":                  "pid",
}


def flatten_value(v, sep):
    if isinstance(v, list):
        return sep.join(map(str, v))
    return v


def memaxes_row(rec, list_sep):
    row = {col: "??" for col in MEMAXES_COLUMNS}
    row["virt_addr_offset"] = "0"
    for cali_key, mx_col in MEMAXES_FROM_CALI.items():
        if cali_key in rec:
            row[mx_col] = flatten_value(rec[cali_key], list_sep)
    return row


def main():
    ap = argparse.ArgumentParser(description="Convert Caliper .cali to CSV")
    ap.add_argument("input", help="Input .cali file")
    ap.add_argument("output", nargs="?", default="output.csv", help="Output CSV file (positional)")
    ap.add_argument("-o", "--output-flag", dest="output_flag", help="Output CSV file (overrides positional)")
    ap.add_argument("--delimiter", default=",", help="CSV delimiter (default: ,)")
    ap.add_argument("--list-sep", default="/", help="Separator for list values")
    ap.add_argument("--columns", help="Comma-separated list of columns to include (raw mode only)")
    ap.add_argument("--no-header", action="store_true", help="Do not write header row")
    ap.add_argument("--memaxes", action="store_true",
                    help="Emit mitos samples.csv-compatible columns for MemAxes")
    ap.add_argument("--sources-out", metavar="PATH",
                    help="Write unique source file paths (one per line) to this file")

    args = ap.parse_args()
    output = args.output_flag if args.output_flag else args.output

    reader = cr.CaliperReader()
    reader.read(args.input)
    records = reader.records

    if args.memaxes:
        fieldnames = MEMAXES_COLUMNS
        built_rows = [memaxes_row(rec, args.list_sep) for rec in records]
    else:
        if args.columns:
            fieldnames = [c.strip() for c in args.columns.split(",")]
        else:
            fieldnames = sorted({k for rec in records for k in rec.keys()})
        built_rows = [
            {k: flatten_value(rec.get(k, ""), args.list_sep) for k in fieldnames}
            for rec in records
        ]

    with open(output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, delimiter=args.delimiter)
        if not args.no_header:
            writer.writeheader()
        writer.writerows(built_rows)

    if args.sources_out:
        src_col = "source" if args.memaxes else "source.file#libpfm.ip"
        sources = sorted({
            row[src_col]
            for row in built_rows
            if src_col in row and row[src_col] not in ("", "??")
        })
        with open(args.sources_out, "w") as sf:
            sf.write("\n".join(sources))
            if sources:
                sf.write("\n")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
