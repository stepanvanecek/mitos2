/******************************************************************************
 * * FILE: mpi_mm.c
 * * DESCRIPTION:  
 * *   MPI Matrix Multiply - C Version
 * *   In this code, the master task distributes a matrix multiply
 * *   operation to numtasks-1 worker tasks.
 * *   NOTE:  C and Fortran versions of this code differ because of the way
 * *   arrays are stored/passed.  C arrays are row-major order but Fortran
 * *   arrays are column-major order.
 * * AUTHOR: Blaise Barney. Adapted from Ros Leibensperger, Cornell Theory
 * *   Center. Converted to MPI: George L. Gusciora, MHPCC (1/95)
 * * LAST REVISED: 04/13/05
 * ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <sys/resource.h>

#include "mpi.h"
#include "calimitos.h"

#define NRA 1024                 /* number of rows in matrix A */
#define NCA 1024                 /* number of columns in matrix A */
#define NCB 1024                 /* number of columns in matrix B */
#define MASTER 0                 /* taskid of first task */
#define FROM_MASTER 1            /* setting a message type */
#define FROM_WORKER 2            /* setting a message type */
double	a[NRA][NCA],             /* matrix A to be multiplied */
b[NCA][NCB],           /* matrix B to be multiplied */
c[NRA][NCB];           /* result matrix C */

namespace {

// Pre-allocated attribute for sample-time callback; initialized in main
cali::Attribute g_sample_timestamp_attr;

// Sample callback: runs in signal context. Must stay async-signal-safe.
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

int main (int argc, char *argv[])
{
   int	numtasks,              /* number of tasks in partition */
   	    taskid,                /* a task identifier */
        numworkers,            /* number of worker tasks */
        source,                /* task id of message source */
        dest,                  /* task id of message destination */
        mtype,                 /* message type */
        rows,                  /* rows of matrix A sent to each worker */
        averow, extra, offset, /* used to determine rows sent to each worker */
        i, j, k, rc;           /* misc */
         
    MPI_Init(&argc,&argv);
    MPI_Status status;
    MPI_Comm_rank(MPI_COMM_WORLD,&taskid);
    MPI_Comm_size(MPI_COMM_WORLD,&numtasks);
    if (numtasks < 2 ) {
        fprintf(stderr,"Need at least two MPI tasks. Quitting...\n");
        MPI_Abort(MPI_COMM_WORLD, rc);
        exit(1);
    }
    numworkers = numtasks-1;

    g_sample_timestamp_attr = cali::Caliper::instance().create_attribute(
        "sample.monotonic_ns",
        CALI_TYPE_UINT,
        CALI_ATTR_ASVALUE | CALI_ATTR_SKIP_EVENTS
    );

    Mitos_set_handler_fn(&sample_callback, nullptr);
    // Enable MPI mode so Caliper registers the mpi + mpireport services.
    // mpireport gathers per-rank traces into a single mpireport.cali at flush
    // time; the per-rank trace_rank<N>.cali files come from the recorder
    // service and get a templated filename automatically.
    Mitos_enable_mpi(true);

    Mitos_begin_sampler();

    /**************************** master task ************************************/
    if (taskid == MASTER)
    {
        printf("mpi_mm has started with %d tasks.\n",numtasks);
        printf("Initializing arrays...\n");
        for (i=0; i<NRA; i++)
            for (j=0; j<NCA; j++)
                a[i][j]= i+j;
        for (i=0; i<NCA; i++)
            for (j=0; j<NCB; j++)
                b[i][j]= i*j;

        /* Send matrix data to the worker tasks */
        averow = NRA/numworkers;
        extra = NRA%numworkers;
        offset = 0;
        mtype = FROM_MASTER;
        for (dest=1; dest<=numworkers; dest++)
        {
            rows = (dest <= extra) ? averow+1 : averow;
            printf("Sending %d rows to task %d offset=%d\n",rows,dest,offset);
            MPI_Send(&offset, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
            MPI_Send(&rows, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
            MPI_Send(&a[offset][0], rows*NCA, MPI_DOUBLE, dest, mtype,
                    MPI_COMM_WORLD);
            MPI_Send(&b, NCA*NCB, MPI_DOUBLE, dest, mtype, MPI_COMM_WORLD);
            offset = offset + rows;
        }

        /* Receive results from worker tasks */
        mtype = FROM_WORKER;
        for (i=1; i<=numworkers; i++)
        {
            source = i;
            MPI_Recv(&offset, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
            MPI_Recv(&rows, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
            MPI_Recv(&c[offset][0], rows*NCB, MPI_DOUBLE, source, mtype,
                    MPI_COMM_WORLD, &status);
            printf("Received results from task %d\n",source);
        }
    }

/**************************** worker task ************************************/
    if (taskid > MASTER)
    {
        mtype = FROM_MASTER;
        MPI_Recv(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
        MPI_Recv(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
        MPI_Recv(&a, rows*NCA, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD, &status);
        MPI_Recv(&b, NCA*NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD, &status);

        for (k=0; k<NCB; k++)
            for (i=0; i<rows; i++)
            {
                c[i][k] = 0.0;
                for (j=0; j<NCA; j++)
                c[i][k] = c[i][k] + a[i][j] * b[j][k];
            }
        mtype = FROM_WORKER;
        MPI_Send(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
        MPI_Send(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
        MPI_Send(&c, rows*NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD);
    }

    Mitos_end_sampler();

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        char fname[64];
        std::snprintf(fname, sizeof(fname), "peak_rss_kb_rank%d.txt", taskid);
        if (FILE* f = std::fopen(fname, "w")) {
            std::fprintf(f, "%ld\n", ru.ru_maxrss);
            std::fclose(f);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}
