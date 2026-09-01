/* Dinothawr - one-shot background job. See async_job.h. */

#include <stdlib.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "async_job.h"

struct async_job
{
   sthread_t         *thread;
   async_job_fn       fn;
   void              *userdata;
   void              *result;
   /* Released by the worker after it stores @result, acquired by the
    * poller, so a reader that sees the flag also sees the result. The
    * join in collect would order it too, but the poll has to be safe on
    * its own - that is the whole point of not joining early. */
   retro_atomic_int_t done;
};

static void async_job_thread(void *data)
{
   async_job_t *job = (async_job_t*)data;

   job->result = job->fn(job->userdata);

   retro_atomic_store_release_int(&job->done, 1);
}

async_job_t *async_job_start(async_job_fn fn, void *userdata)
{
   async_job_t *job;

   if (!fn)
      return NULL;

   if (!(job = (async_job_t*)calloc(1, sizeof(*job))))
      return NULL;

   job->fn       = fn;
   job->userdata = userdata;
   job->result   = NULL;
   retro_atomic_store_release_int(&job->done, 0);

   if (!(job->thread = sthread_create(async_job_thread, job)))
   {
      free(job);
      return NULL;
   }

   return job;
}

bool async_job_ready(async_job_t *job)
{
   if (!job)
      return false;
   return retro_atomic_load_acquire_int(&job->done) != 0;
}

void *async_job_collect(async_job_t *job)
{
   void *result;

   if (!job)
      return NULL;

   sthread_join(job->thread);
   result = job->result;
   free(job);

   return result;
}
