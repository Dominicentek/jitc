#ifndef JITC_SCHEDULER_H
#define JITC_SCHEDULER_H

#include "dynamics.h"

typedef void*(*job_func_t)(void* ctx);

void jitc_schedule_job(list_t* jobs, list_t* ctx);

#endif
