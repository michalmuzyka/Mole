#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "defines.h"
#include "index.h"
#include "commands.h"

void usage(char* pname);
void get_args(int argc, char** argv, char* directory_path, char* index_filename, int* periodic_indexing_time);
void work(char* directory_path, char* index_filename, int periodic_indexing_time);

int main(int argc, char**argv){
    char directory_path[MAXPATHLEN+2];
    char index_filename[MAXPATHLEN+2];
    int periodic_indexing_time=0;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE); 
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL)) ERR("SIG_BLOCK");

    get_args(argc, argv, directory_path, index_filename, &periodic_indexing_time);
    work(directory_path, index_filename, periodic_indexing_time);

    if (pthread_sigmask(SIG_UNBLOCK, &mask, NULL)) ERR("SIG_UNBLOCK");

    return EXIT_SUCCESS;
}

//--------------------------------------------------------------

void work(char * directory_path, char * index_filename, int periodic_indexing_time){
    int quit_flag;
    thread_args_t thread_args = {
        .directory_path = directory_path,
        .fileindex_path = index_filename,
        .periodic_indexing_time = periodic_indexing_time,
        .mx_last_time = PTHREAD_MUTEX_INITIALIZER,
        .quit_flag = 0,
        .mx_quit_flag = PTHREAD_MUTEX_INITIALIZER,
        .is_somebody_indexing = 0,
        .mx_is_somebody_indexing = PTHREAD_MUTEX_INITIALIZER,
        .mx_index = PTHREAD_MUTEX_INITIALIZER
    };

    if(!load_index_from_file(index_filename, &thread_args.last_time, &thread_args.index)){
        thread_args.is_somebody_indexing = 1;
        run_detached_indexing(&thread_args);
    }

    if(periodic_indexing_time!=0) 
        if(pthread_create(&thread_args.manager_tid, NULL, periodic_indexing, &thread_args)) ERR("pthread_create");

    run_commandline(&thread_args);

    if(periodic_indexing_time!=0) 
        if(pthread_join(thread_args.manager_tid, NULL)) ERR("pthread_join");
      
    do{
        pthread_mutex_lock(&thread_args.mx_is_somebody_indexing);
        quit_flag = thread_args.is_somebody_indexing;
        pthread_mutex_unlock(&thread_args.mx_is_somebody_indexing);

        msleep(10);
    } while(quit_flag); 
    free(thread_args.index.elements);
}

void usage(char *pname){
	fprintf(stderr,"USAGE: %s (-d directory_path) (-f index_filename) [-t n]\n", pname);
    fprintf(stderr,"* directory_path: If not specified, path stored in $MOLE_DIR is used\n");
    fprintf(stderr,"* index_filename: If not specified, filename stored in $MOLE_INDEX_PATH is used, if $MOLE_INDEX_PATH is not set, file ~/.mole-index is used\n");
    fprintf(stderr,"* n: number of seconds between indexing. If not specified, periodic indexing is disabled\n");
	exit(EXIT_FAILURE);
}

void get_args(int argc, char** argv, char * directory_path, char * index_filename, int*periodic_indexing_time){
    char c;
    int is_d_set = 0, is_f_set = 0, lenght;
    char* path;
    char* home_path;

	while ((c = getopt (argc, argv, "d:f:t:")) != -1){
		switch (c){
			case 'd':
				is_d_set = 1;
                if(!strncpy(directory_path, optarg, MAXPATHLEN+1)) ERR("strncpy");
                if(strlen(directory_path) > MAXPATHLEN) ERR("PATH GIVEN BY -d IS TOO LONG\n");
                break;
			case 'f':
				is_f_set = 1;
                if(!strncpy(index_filename, optarg, MAXPATHLEN+1)) ERR("strncpy");
                if(strlen(index_filename) > MAXPATHLEN) ERR("FILENAME GIVEN BY -f IS TOO LONG\n");
                break;
			case 't':
				*periodic_indexing_time = atoi(optarg);
                if(*periodic_indexing_time < 30 || *periodic_indexing_time > 7200)
                    usage(argv[0]);
                break;
			case '?':
			default: 
                usage(argv[0]);
		}
    }

	if(argc>optind) usage(argv[0]);

    if(!is_d_set){ 
        if(!(path = getenv("MOLE_DIR"))) usage(argv[0]);

        if(!strncpy(directory_path, path, MAXPATHLEN+1)) ERR("strncpy");
        if(strlen(directory_path) > MAXPATHLEN) ERR("PATH IN $MOLE_DIR IS TOO LONG\n");
    }

    if(!is_f_set){
        if((path = getenv("MOLE_INDEX_PATH"))){
            if(!strncpy(index_filename, path, MAXPATHLEN+1)) ERR("strncpy");
            if(strlen(index_filename) > MAXPATHLEN) ERR("PATH IN $MOLE_INDEX_PATH IS TOO LONG\n");
        } else {
            lenght = strlen("/.mole-index");
            if(!(home_path = getenv("HOME"))) ERR("getenv(HOME)");
            if(!strncpy(index_filename, home_path, MAXPATHLEN - lenght + 1)) ERR("strncpy");
            if(strlen(index_filename) > MAXPATHLEN - lenght) ERR("HOME PATH IS TOO LONG\n");
            if(!strcat(index_filename, "/.mole-index")) ERR("strcat"); 
        }
    }
}