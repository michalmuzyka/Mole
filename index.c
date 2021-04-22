#define _GNU_SOURCE
#include <ftw.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include "index.h"
#include "err.h"
#define ELAPSED(start,end) ((end).tv_sec-(start).tv_sec)+(((end).tv_nsec - (start).tv_nsec) * 1.0e-9)

//-----------------------FOR NFTW--------------------------------------------
#define MAXFD 20
index_vector_t tmp_index_vector;

int walk(const char *name, const struct stat *s, int ftw_type, struct FTW *f)
{
    FILE_TYPE type_id = -1;

    switch (ftw_type){
        case FTW_DNR:
        case FTW_D: 
            type_id = DIR; 
        break;

        case FTW_F:
            type_id = get_file_type(name);
        break;

        default:
        break;
    }

    if(type_id != -1){
        push_to_index(&tmp_index_vector, name, type_id, s);
    }
    
    return 0;
}

FILE_TYPE get_file_type(const char* name){
    file_signature_t file_signatures[FILETYPECOUNT-1] = {
        {PNG, 8, {137, 80, 78, 71, 13, 10, 26, 10}},
        {JPEG, 3, {255, 216, 255}},
        {GZIP, 2, {31, 139}},
        {ZIP, 2, {80, 75}}
    };

    int i, j;
    int file;
    int count;
    FILE_TYPE type_id = -1;
    unsigned char buffer[MAXSIGNATURESIZE];

    if((file = TEMP_FAILURE_RETRY(open(name, O_RDONLY)))<0) ERR("open");
    if((count = TEMP_FAILURE_RETRY(read(file, buffer, MAXSIGNATURESIZE))) < 0) ERR("read");

    for(i=0; i<FILETYPECOUNT-1; ++i){
        if(count >= file_signatures[i].size){
            type_id = i;
            for(j=0; j<file_signatures[i].size; ++j){
                if(buffer[j] != file_signatures[i].signature[j]){
                    type_id = -1;
                    break;
                }
            }
            if(type_id != -1) return type_id;
        }
    }
    if(TEMP_FAILURE_RETRY(close(file))) ERR("close");
    return -1;
}

//------------------------INDEXING DIRECTORY-------------------------------------

void run_detached_indexing(thread_args_t* args){
    pthread_attr_t threadAttr;
    if(pthread_attr_init(&threadAttr)) ERR("pthread_attr_init");
    if(pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED)) ERR("pthread_attr_setdetachsatate");
    if(pthread_create(&(args->indexer_tid), &threadAttr, index_directory, (void*)args)) ERR("pthread_create");
    pthread_attr_destroy(&threadAttr);
}

void* index_directory(void *vargs){
    pthread_cleanup_push(clean_up, vargs);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    thread_args_t* args = vargs;

    init_index(&tmp_index_vector);

    if(nftw(args->directory_path, walk, MAXFD, FTW_PHYS) == -1) ERR("NFTW");

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    save_index_to_file(args->fileindex_path);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    pthread_mutex_lock(&args->mx_index);
    free(args->index.elements);
    args->index.length = tmp_index_vector.length;
    args->index.max_size = tmp_index_vector.max_size;
    args->index.elements = tmp_index_vector.elements;
    tmp_index_vector.elements = NULL;
    pthread_mutex_unlock(&args->mx_index);

    pthread_mutex_lock(&(args->mx_last_time));
    if(clock_gettime(CLOCK_REALTIME, &(args->last_time))) ERR("clock_getttime");
    pthread_mutex_unlock(&(args->mx_last_time));

    printf("Indexing finished!\n");

    pthread_cleanup_pop(1);
    return NULL;
}

void clean_up(void* vargs){
    thread_args_t* args = vargs;
    pthread_mutex_lock(&args->mx_is_somebody_indexing);
    args->is_somebody_indexing = 0;
    pthread_mutex_unlock(&args->mx_is_somebody_indexing);    
}

//--------------------------------------------------------------------------------

void* periodic_indexing(void* vargs){
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    thread_args_t* args = vargs;
    int quit_flag = 0; 
    timespec_t current, last;

    while(!quit_flag){
        if(clock_gettime(CLOCK_REALTIME, &current)) ERR("clock_gettime");

        pthread_mutex_lock(&args->mx_last_time);
        last = args->last_time;
        pthread_mutex_unlock(&args->mx_last_time);

        pthread_mutex_lock(&args->mx_is_somebody_indexing);
        if(!args->is_somebody_indexing && ELAPSED(last, current) > args->periodic_indexing_time){
            args->is_somebody_indexing = 1;
            pthread_mutex_unlock(&args->mx_is_somebody_indexing);

            args->indexer_tid = args->manager_tid;
            index_directory(args);
        } 
        else pthread_mutex_unlock(&args->mx_is_somebody_indexing);

        msleep(10);

        pthread_mutex_lock(&args->mx_quit_flag);
        quit_flag = args->quit_flag;
        pthread_mutex_unlock(&args->mx_quit_flag);        
    }

    return NULL;
}

//--------------------------------------------------------------------------------

int load_index_from_file(char* f_path, timespec_t* last_indexing, index_vector_t*index){
    int file;
    int i, count;
    struct stat stats;

    if((file = TEMP_FAILURE_RETRY(open(f_path, O_CREAT | O_EXCL, 0777)))<0 && errno != EEXIST) ERR("open");
    if(file >= 0){ 
        if(TEMP_FAILURE_RETRY(close(file))) ERR("close");
        return 0;
    }

    if((file = TEMP_FAILURE_RETRY(open(f_path, O_RDONLY)))<0) ERR("open");
    if((count = TEMP_FAILURE_RETRY(read(file, &(index->length), sizeof(int)))) < 0) ERR("read");
    
    if(count != sizeof(int) || index->length < 0){
        if(close(file)) ERR("close");
        return 0;
    }

    index->max_size = index->length;
    if(!(index->elements = malloc(sizeof(index_element_t) * index->length))) ERR("malloc");

    for(i=0; i<index->length; ++i){
        if((count = TEMP_FAILURE_RETRY(read(file, index->elements+i, sizeof(index_element_t)))) < 0) ERR("read");
    }

    if(fstat(file, &stats)) ERR("fstat");
    (*last_indexing) = stats.st_mtim;
    
    if(TEMP_FAILURE_RETRY(close(file))) ERR("close");
    return 1;
}

void save_index_to_file(char* f_path){
    int file;
    int i;

    if((file = TEMP_FAILURE_RETRY(open(f_path, O_WRONLY|O_CREAT|O_TRUNC, 0777)))<0) ERR("open");
    if(TEMP_FAILURE_RETRY(write(file, &(tmp_index_vector.length), sizeof(int)))<0) ERR("write"); 

    for(i=0; i<tmp_index_vector.length; ++i){
        if(TEMP_FAILURE_RETRY(write(file, tmp_index_vector.elements + i, sizeof(index_element_t)))<0) ERR("write");
    }

    if(TEMP_FAILURE_RETRY(close(file))) ERR("close");
}

//------------------------VECTOR MANIPULATION-----------------------------

void init_index(index_vector_t* index){
    index->length = 0;
    index->max_size = VECTORINITSIZE;
    if(!(index->elements = malloc(sizeof(index_element_t)*index->max_size))) ERR("malloc");
}

void expand_index(index_vector_t* index){
    index->max_size *= 2;
    index_element_t* tmp = realloc(index->elements, sizeof(index_element_t) * index->max_size);
    if(!tmp){
        free(index->elements);
        ERR("realloc");
    }
    index->elements = tmp;
}

void push_to_index(index_vector_t* index, const char* name, FILE_TYPE type_id, const struct stat *s){
    char duplicated_name[MAXPATHLEN];
    char *base;
    int i = tmp_index_vector.length;

    if(strlen(name) > MAXPATHLEN - 1){
        fprintf(stderr, "THIS PATH IS TOO LONG: %s\n", name);
        return;
    }
    if(!strncpy(duplicated_name, name, MAXPATHLEN)) ERR("strcpy");
    base = basename(duplicated_name);
    if(strlen(base) > MAXFILENAMELEN - 1){
        fprintf(stderr, "THIS FILENAME IS TOO LONG: %s\n", base);
        return;
    }

    if(tmp_index_vector.length == tmp_index_vector.max_size)
        expand_index(&tmp_index_vector);

    if(!memset(&tmp_index_vector.elements[i], '\0', sizeof(index_element_t))) ERR("memset");

    tmp_index_vector.elements[i].type = type_id;
    if(!strcpy(tmp_index_vector.elements[i].full_path, name)) ERR("strcpy");
    if(!strcpy(tmp_index_vector.elements[i].filename, base)) ERR("strcpy");
    tmp_index_vector.elements[i].ownerUID = s->st_uid;
    tmp_index_vector.elements[i].size = s->st_size;

    tmp_index_vector.length++;
}


void delete_index(index_vector_t* index){
    free(index->elements);
}

//-----------------------------------------------------------------------

void msleep(unsigned int milisec) {
    time_t sec = (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req = {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req, &req)) ERR("nanosleep");
}