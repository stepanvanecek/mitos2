#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <sys/resource.h>

#include "calimitos.h"

#define ROW_MAJOR(x,y,width) y*width+x

namespace {

// Pre-allocated attribute for sample-time callback; initialized in main
cali::Attribute g_sample_timestamp_attr;

// Sample callback: runs in signal context, captures monotonic timestamp as ns.
// Must stay async-signal-safe (no malloc, no string ops).
void sample_callback(cali::Caliper&        /*c*/,
                     cali::SnapshotView    /*trigger*/,
                     cali::SnapshotBuilder& rec,
                     void*                  /*args*/)
{
    const uint64_t ns = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    rec.append(g_sample_timestamp_attr, cali::Variant(cali_make_variant_from_uint(ns)));
}

} // namespace

void init_matrices(int N, double **a, double **b, double **c)
{
    int i,j,k;

    *a = new double[N*N];
    *b = new double[N*N];
    *c = new double[N*N];

    for(i=0; i<N; ++i)
    {
        for(j=0; j<N; ++j)
        {
            (*a)[ROW_MAJOR(i,j,N)] = (double)rand();
            (*b)[ROW_MAJOR(i,j,N)] = (double)rand();
            (*c)[ROW_MAJOR(i,j,N)] = 0;
        }
    }
}

void matmul(int N, double *a, double *b, double *c)
{

    for(int i=0; i<N; ++i)
    {
        for(int j=0; j<N; ++j)
        {
            for(int k=0; k<N; ++k)
            {
                c[ROW_MAJOR(i,j,N)] += a[ROW_MAJOR(i,k,N)]*b[ROW_MAJOR(k,j,N)];
            }
        }
    }

    int randx = rand() % N;
    int randy = rand() % N;
    std::cout << c[ROW_MAJOR(randx,randy,N)] << std::endl;
}

int main(int argc, char **argv)
{
    int N = (argc == 2) ? atoi(argv[1]) : 1024;
    double *a,*b,*c;

    // Pre-create attribute for sample-time callback (must happen before sampling starts)
    g_sample_timestamp_attr = cali::Caliper::instance().create_attribute(
        "sample.monotonic_ns",
        CALI_TYPE_UINT,
        CALI_ATTR_ASVALUE | CALI_ATTR_SKIP_EVENTS
    );

    // Register sample callback to capture timestamps at sample time
    Mitos_set_handler_fn(&sample_callback, nullptr);

    Mitos_begin_sampler();

    init_matrices(N,&a,&b,&c);
    matmul(N,a,b,c);

    Mitos_end_sampler();

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        if (FILE* f = std::fopen("peak_rss_kb.txt", "w")) {
            std::fprintf(f, "%ld\n", ru.ru_maxrss);
            std::fclose(f);
        }
    }

    return 0;
}
