#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

threadpool* create_threadpool(int num_threads_in_pool, int max_queue_size){
    if (num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool <= 0){
        printf("max number of threads in pool should be between 1 and %d",MAXT_IN_POOL);
        return NULL;
    }
    else if (max_queue_size > MAXW_IN_QUEUE || max_queue_size <= 0){
        printf("max size of the queue should be between 1 and %d",MAXW_IN_QUEUE);
        return NULL;
    }
    threadpool *tp;
    tp = (threadpool*)malloc(sizeof(threadpool));
    if (tp == NULL){
        perror("tp malloc");
        return NULL;
    }

    tp->num_threads = num_threads_in_pool;
    tp->max_qsize = max_queue_size;
    tp->qsize = 0;
    tp->qhead = NULL;
    tp->qtail = NULL;
    pthread_mutex_init(&tp->qlock,NULL);
    pthread_cond_init(&tp->q_not_empty,NULL);
    pthread_cond_init(&tp->q_empty,NULL);
    pthread_cond_init(&tp->q_not_full,NULL);
    tp->shutdown = 0;
    tp->dont_accept = 0;

    tp->threads = (pthread_t*)malloc(num_threads_in_pool * sizeof(pthread_t));
    if (tp->threads == NULL) {
        perror("malloc for threads array");
        pthread_mutex_destroy(&tp->qlock);
        pthread_cond_destroy(&tp->q_not_empty);
        pthread_cond_destroy(&tp->q_empty);
        pthread_cond_destroy(&tp->q_not_full);
        free(tp);
        return NULL;
    }

    for (int t = 0; t < num_threads_in_pool; t++) {
        int rc = pthread_create(&tp->threads[t], NULL, do_work, tp);
        if (rc) {
            perror("thread create");
            // Clean up previously created threads
            for (int i = 0; i < t; i++) {
                pthread_cancel(tp->threads[i]);
            }
            free(tp->threads);
            pthread_mutex_destroy(&tp->qlock);
            pthread_cond_destroy(&tp->q_not_empty);
            pthread_cond_destroy(&tp->q_empty);
            pthread_cond_destroy(&tp->q_not_full);
            free(tp);
            return NULL;
        }
    }
    return tp;
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg){
    pthread_mutex_lock(&from_me->qlock);
    if(from_me->dont_accept){
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }

    // Wait if the queue is full
    while (from_me->qsize == from_me->max_qsize) {
        pthread_cond_wait(&from_me->q_not_full, &from_me->qlock);
    }

    work_t *work = (work_t*)malloc(sizeof(work_t));
    if (work == NULL) {
        perror("malloc for work_t");
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }

    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;
    // Enqueue the work
    if (from_me->qtail == NULL) { //queue is empty
        from_me->qhead = from_me->qtail = work;
    } else {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }
    from_me->qsize++;

    pthread_cond_signal(&from_me->q_not_empty);
    pthread_mutex_unlock(&from_me->qlock);
}

void* do_work(void* p){
    threadpool* tp = (threadpool*)p;
    while(1){
        pthread_mutex_lock(&tp->qlock);
        if(tp->shutdown){
            pthread_mutex_unlock(&tp->qlock);
            pthread_exit(NULL);
        }

        if(tp->qsize == 0){
            pthread_cond_wait(&tp->q_not_empty,&tp->qlock);
        }

        if(tp->shutdown){
            pthread_mutex_unlock(&tp->qlock);
            pthread_exit(NULL);
        }

        //dequeue
        work_t *work = tp->qhead;
        tp->qhead = work->next;
        tp->qsize--;
        if (tp->qhead == NULL) { //queue is empty
            tp->qtail = NULL;
        }

        if (tp->qsize == tp->max_qsize - 1){
            pthread_cond_signal(&tp->q_not_full);
        }

        if(tp->qsize == 0 && tp->dont_accept){
            pthread_cond_signal(&tp->q_empty);
        }

        pthread_mutex_unlock(&tp->qlock);
        work->routine(work->arg);
        free(work);
    }
}

void destroy_threadpool(threadpool* destroyme){
    if (destroyme == NULL) return;
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = 1;

    //wait for the queue to empty
    while(destroyme->qsize > 0){
        pthread_cond_wait(&destroyme->q_empty,&destroyme->qlock);
    }

    destroyme->shutdown = 1;
    //wake up all threads that wait while the qsize = 0
    pthread_cond_broadcast(&destroyme->q_not_empty);
    pthread_mutex_unlock(&destroyme->qlock);
    for (int i = 0; i < destroyme->num_threads; ++i) {
        pthread_join(destroyme->threads[i], NULL);
    }

    work_t* current_work = destroyme->qhead;
    while (current_work != NULL) {
        work_t* temp = current_work;
        current_work = current_work->next;
        free(temp);
    }

    pthread_mutex_destroy(&destroyme->qlock);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_cond_destroy(&destroyme->q_empty);
    pthread_cond_destroy(&destroyme->q_not_full);

    free(destroyme->threads);
    free(destroyme);
}



