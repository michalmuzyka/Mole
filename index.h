#ifndef HEADER_INDEX
#define HEADER_INDEX

#define _GNU_SOURCE
#include <ftw.h>
#include <sys/types.h>
#include <time.h>
#include "defines.h"

#define FILETYPECOUNT 5
#define MAXSIGNATURESIZE 8

typedef enum FILE_TYPE{
    PNG = 0,
    JPEG = 1,
    GZIP = 2,
    ZIP = 3,
    DIR = 4
} FILE_TYPE;

//--------------------------------

typedef struct file_signature{
    FILE_TYPE id;
    int size;
    unsigned char signature[MAXSIGNATURESIZE];
} file_signature_t;

//--------------------------------

typedef struct index_element{
   char filename[MAXFILENAMELEN];
   char full_path[MAXPATHLEN];
   off_t size;
   uid_t ownerUID;
   FILE_TYPE type;
} index_element_t;

typedef struct index_vector{
    index_element_t* elements;
    int length;
    int max_size;
} index_vector_t;

//--------------------------------

typedef struct timespec timespec_t;
typedef struct thread_args{
    pthread_t manager_tid;
    pthread_t indexer_tid;

    char* directory_path;
    char* fileindex_path;
    int periodic_indexing_time;

    timespec_t last_time;
    pthread_mutex_t mx_last_time;

    int quit_flag;
    pthread_mutex_t mx_quit_flag;

    int is_somebody_indexing;
    pthread_mutex_t mx_is_somebody_indexing;

    index_vector_t index;
    pthread_mutex_t mx_index; 
} thread_args_t;

//--------------------------------

void init_index(index_vector_t* index);
void expand_index(index_vector_t* index);
void push_to_index(index_vector_t* index, const char* name, FILE_TYPE type_id, const struct stat *s);
void delete_index(index_vector_t* index);

void save_index_to_file(char* f_path);
int load_index_from_file(char* f_path, timespec_t* last_indexing, index_vector_t*index); 

void* index_directory(void* args);
void clean_up(void* args);
void run_detached_indexing(thread_args_t* args);

void* periodic_indexing(void* args);

FILE_TYPE get_file_type(const char* name);
int walk(const char* name, const struct stat* s, int ftw_type, struct FTW* f);
void msleep(unsigned int milisec);

#endif