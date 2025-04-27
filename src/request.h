#pragma once

#define DEFAULT_BUFFER_SIZE 64
#define DEFAULT_THREADS 4
#define DEFAULT_SCHED_ALGO 0		// 0 - FIFO, 1 - SFF, 2 - RANDOM

extern int buffer_max_size;
extern int buffer_size;
extern int scheduling_algo;
extern int num_threads;

void request_handle(int fd);
void* thread_request_serve_static(void* arg);
