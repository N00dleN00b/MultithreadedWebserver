#include "io_helper.h"
#include "request.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MAXBUF (8192)

// Scheduler config (from request.h)
int num_threads = DEFAULT_THREADS;
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int scheduling_algo = DEFAULT_SCHED_ALGO;

typedef struct {
    int fd;
    int filesize;
} request_t;

request_t request_buffer[DEFAULT_BUFFER_SIZE];
int buffer_start = 0, buffer_end = 0, buffer_count = 0;

pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];

    sprintf(body, "<!doctype html>\r\n"
                  "<head><title>WebServer Error</title></head>\r\n"
                  "<body><h2>%s: %s</h2><p>%s: %s</p></body>\r\n</html>\r\n",
            errnum, shortmsg, longmsg, cause);
    
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    write_or_die(fd, body, strlen(body));
    close_or_die(fd);
}

void request_read_headers(int fd) {
    char buf[MAXBUF];
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
        readline_or_die(fd, buf, MAXBUF);
    }
}

int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    if (!strstr(uri, "cgi")) {
        strcpy(cgiargs, "");
        sprintf(filename, ".%s", uri);
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "index.html");
        return 1;
    } else {
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        } else {
            strcpy(cgiargs, "");
        }
        sprintf(filename, ".%s", uri);
        return 0;
    }
}

void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else strcpy(filetype, "text/plain");
}

void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];

    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);

    sprintf(buf, "HTTP/1.0 200 OK\r\n"
                 "Server: Multithreaded WebServer\r\n"
                 "Content-Length: %d\r\n"
                 "Content-Type: %s\r\n\r\n",
            filesize, filetype);

    write_or_die(fd, buf, strlen(buf));
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

void* thread_request_serve_static(void* arg) {
    srand(time(NULL) ^ pthread_self()); // Seed randomness

    while (1) {
        pthread_mutex_lock(&buffer_lock);
        while (buffer_count == 0)
            pthread_cond_wait(&buffer_not_empty, &buffer_lock);

        int index = buffer_start;
        if (scheduling_algo == 2) {
            // Random
            int r = rand() % buffer_count;
            index = (buffer_start + r) % buffer_max_size;
        }

        request_t req = request_buffer[index];

        // Remove request
        if (index == buffer_start) {
            buffer_start = (buffer_start + 1) % buffer_max_size;
        } else {
            for (int i = index; i != buffer_start; i = (i - 1 + buffer_max_size) % buffer_max_size) {
                request_buffer[i] = request_buffer[(i - 1 + buffer_max_size) % buffer_max_size];
            }
            buffer_start = (buffer_start + 1) % buffer_max_size;
        }

        buffer_count--;
        pthread_cond_signal(&buffer_not_full);
        pthread_mutex_unlock(&buffer_lock);

        request_handle(req.fd);
        close_or_die(req.fd);
    }
}

void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];

    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    if (strcasecmp(method, "GET")) {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
        return;
    }

    request_read_headers(fd);
    is_static = request_parse_uri(uri, filename, cgiargs);

    if (stat(filename, &sbuf) < 0) {
        request_error(fd, filename, "404", "Not found", "file not found on server");
        return;
    }

    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            request_error(fd, filename, "403", "Forbidden", "cannot read this file");
            return;
        }

        if (strstr(filename, "..")) {
            request_error(fd, filename, "403", "Forbidden", "directory traversal attempt blocked");
            return;
        }

        pthread_mutex_lock(&buffer_lock);
        while (buffer_count == buffer_max_size)
            pthread_cond_wait(&buffer_not_full, &buffer_lock);

        request_t new_request = { fd, sbuf.st_size };

        if (scheduling_algo == 0 || scheduling_algo == 2) {
            // FIFO or Random
            request_buffer[buffer_end] = new_request;
            buffer_end = (buffer_end + 1) % buffer_max_size;
        } else if (scheduling_algo == 1) {
            // SFF: insert sorted by filesize
            int i = buffer_count - 1;
            int idx = (buffer_start + i) % buffer_max_size;
            while (i >= 0 && request_buffer[idx].filesize > new_request.filesize) {
                request_buffer[(buffer_start + i + 1) % buffer_max_size] = request_buffer[idx];
                i--;
                idx = (idx - 1 + buffer_max_size) % buffer_max_size;
            }
            request_buffer[(buffer_start + i + 1) % buffer_max_size] = new_request;
            buffer_end = (buffer_start + buffer_count + 1) % buffer_max_size;
        }

        buffer_count++;
        pthread_cond_signal(&buffer_not_empty);
        pthread_mutex_unlock(&buffer_lock);
        return;
    } else {
        request_error(fd, filename, "501", "Not Implemented", "dynamic content not supported");
    }
}