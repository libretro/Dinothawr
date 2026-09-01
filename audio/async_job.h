/* Dinothawr - one-shot background job.
 *
 * A job runs a function once on an rthreads thread and holds its result
 * until the caller takes it. The caller polls async_job_ready() from its
 * normal per-frame work and calls async_job_collect() once that returns
 * true, so nothing on the game thread waits on the decode.
 *
 * Collecting joins the thread, so a job whose function has not finished
 * blocks until it does. Poll first for the non-blocking path; collect
 * directly only when tearing down, where waiting is the point.
 *
 * The job does not own @userdata: keep it alive until collect returns.
 *
 * MSVC C89.
 */

#ifndef ASYNC_JOB_H__
#define ASYNC_JOB_H__

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct async_job async_job_t;

/* The work itself. Whatever it returns is what async_job_collect()
 * hands back; NULL is a perfectly good "it failed". */
typedef void *(*async_job_fn)(void *userdata);

/* Start @fn on its own thread. Returns NULL if the thread could not be
 * created, in which case nothing has run and @fn is the caller's to run
 * itself if it still wants the result. */
async_job_t *async_job_start(async_job_fn fn, void *userdata);

/* True once the function has returned and collecting will not block. */
bool async_job_ready(async_job_t *job);

/* Join, free the job, and return what the function returned. Blocks if
 * the function is still running. Passing NULL returns NULL. */
void *async_job_collect(async_job_t *job);

#ifdef __cplusplus
}
#endif

#endif
