#ifndef HEADER_COMMANDS
#define HEADER_COMMANDS

#include "index.h"
#include "err.h"

typedef struct command{
    char* name;
    int allow_additional_args;
    void (*command_fun)(thread_args_t* args, char*rest_command);
} command_t;

typedef struct int_vector{
    int* elements;
    int length;
    int max_size;
} int_vector_t;

void run_commandline(thread_args_t* args);

void command_exit(thread_args_t* args, char* mod);
void command_exitForced(thread_args_t* args, char* mod);
void command_index(thread_args_t* args, char* mod);
void command_count(thread_args_t* args, char* mod);
void command_namepart(thread_args_t* args, char* mod);
void command_largerthan(thread_args_t* args, char* mod);
void command_owner(thread_args_t* args, char* mod);

void show_result(thread_args_t* args, int_vector_t* vec);

void init_vec(int_vector_t* vec);
void expand_vec(int_vector_t* vec);
void push_to_vec(int_vector_t* vec, int elem);
void delete_vec(int_vector_t* vec);

int is_number(char*s);

#endif