#include "jitc_internal.h"

#include <stdbool.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>

typedef struct {
    size_t counter;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} job_group_t;

typedef struct {
    job_group_t* group;
    job_func_t func;
    void* context;
    void** writeback;
} job_t;

typedef struct {
    pthread_t thread;
    job_t* curr_job;
    pthread_mutex_t wakeup_mutex;
    pthread_cond_t wakeup_cond;
} worker_t;

size_t num_threads;
worker_t* workers;
list_t* waiting_jobs;
pthread_mutex_t scheduler_lock, scheduler_wakeup_lock, scheduler_new_job_lock;
pthread_cond_t scheduler_wakeup_cond, scheduler_new_job_cond;
pthread_t scheduler_thread;
size_t free_workers = 0;

static void* worker_func(void* param) {
    worker_t* worker = param;
    while (true) {
        pthread_mutex_lock(&worker->wakeup_mutex);
        while (!worker->curr_job) pthread_cond_wait(&worker->wakeup_cond, &worker->wakeup_mutex);
        pthread_mutex_unlock(&worker->wakeup_mutex);

        pthread_mutex_lock(&scheduler_lock);
        job_t* job = worker->curr_job;
        pthread_mutex_unlock(&scheduler_lock);

        *job->writeback = job->func(job->context);

        pthread_mutex_lock(&job->group->lock);
        job->group->counter++;
        pthread_cond_signal(&job->group->cond);
        pthread_mutex_unlock(&job->group->lock);

        pthread_mutex_lock(&scheduler_lock);
        free(job);
        worker->curr_job = NULL;
        free_workers++;
        pthread_mutex_unlock(&scheduler_lock);

        pthread_mutex_lock(&scheduler_wakeup_lock);
        pthread_cond_signal(&scheduler_wakeup_cond);
        pthread_mutex_unlock(&scheduler_wakeup_lock);
    }
    return NULL;
}

static void* scheduler_func(void* param) {
    while (true) {
        if (list_size(waiting_jobs) == 0) {
            pthread_mutex_lock(&scheduler_new_job_lock);
            pthread_cond_wait(&scheduler_new_job_cond, &scheduler_new_job_lock);
            pthread_mutex_unlock(&scheduler_new_job_lock);
        }
        pthread_mutex_lock(&scheduler_lock);
        for (size_t i = 0; i < num_threads && free_workers != 0; i++) {
            if (workers[i].curr_job) continue;
            size_t index = rand() % list_size(waiting_jobs);
            workers[i].curr_job = list_get_ptr(waiting_jobs, index);
            list_remove(waiting_jobs, index);
            pthread_mutex_lock(&workers[i].wakeup_mutex);
            pthread_cond_signal(&workers[i].wakeup_cond);
            pthread_mutex_unlock(&workers[i].wakeup_mutex);
            free_workers--;
        }
        pthread_mutex_unlock(&scheduler_lock);
        pthread_mutex_lock(&scheduler_wakeup_lock);
        while (free_workers == 0) pthread_cond_wait(&scheduler_wakeup_cond, &scheduler_wakeup_lock);
        pthread_mutex_unlock(&scheduler_wakeup_lock);
    }
    return NULL;
}

static void jitc_scheduler_init() {
    num_threads = free_workers = sysconf(_SC_NPROCESSORS_ONLN);
    workers = malloc(sizeof(worker_t) * num_threads);
    for (size_t i = 0; i < num_threads; i++) {
        pthread_create(&workers[i].thread, NULL, worker_func, &workers[i]);
        pthread_mutex_init(&workers[i].wakeup_mutex, NULL);
        pthread_cond_init(&workers[i].wakeup_cond, NULL);
        workers[i].curr_job = NULL;
    }
    pthread_mutex_init(&scheduler_lock, NULL);
    pthread_mutex_init(&scheduler_wakeup_lock, NULL);
    pthread_mutex_init(&scheduler_new_job_lock, NULL);
    pthread_cond_init(&scheduler_wakeup_cond, NULL);
    pthread_cond_init(&scheduler_new_job_cond, NULL);
    pthread_create(&scheduler_thread, NULL, scheduler_func, NULL);
    waiting_jobs = list_new();
}

void jitc_schedule_job(list_t* jobs, list_t* ctx) {
    if (num_threads == 0) jitc_scheduler_init();
    pthread_mutex_lock(&scheduler_lock);
    job_group_t* group = malloc(sizeof(job_group_t));
    pthread_mutex_init(&group->lock, NULL);
    pthread_cond_init(&group->cond, NULL);
    group->counter = 0;
    bool is_empty = list_size(waiting_jobs) == 0;
    for (size_t i = 0; i < list_size(jobs); i++) {
        job_t* job = malloc(sizeof(job_t));
        job->func = list_get_ptr(jobs, i);
        job->context = ctx ? list_get_ptr(ctx, i) : NULL;
        job->writeback = list_get(jobs, i);
        job->group = group;
        list_add_ptr(waiting_jobs, job);
    }
    pthread_mutex_unlock(&scheduler_lock);
    if (is_empty) {
        pthread_mutex_lock(&scheduler_new_job_lock);
        pthread_cond_signal(&scheduler_new_job_cond);
        pthread_mutex_unlock(&scheduler_new_job_lock);
    }
    pthread_mutex_lock(&group->lock);
    while (group->counter < list_size(jobs)) pthread_cond_wait(&group->cond, &group->lock);
    pthread_mutex_unlock(&group->lock);
    free(group);
}
