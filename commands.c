#include "commands.h"
#include <pthread.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>

#define COMMANDSCOUNT 7
#define MAXCOMMANDLEN 30

void run_commandline(thread_args_t* args){
    char command[MAXCOMMANDLEN+2];
    char modifiers[MAXCOMMANDLEN+2];
    char *newline;
    char *begin_of_modifiers;

    int modifiers_length;
    int i;
    int quitFlag = 0;

    command_t commands[COMMANDSCOUNT] = {
        {"exit", 0, command_exit},
        {"!exit", 0, command_exitForced},
        {"index", 0, command_index},
        {"count", 0, command_count},
        {"largerthan", 1, command_largerthan},
        {"namepart", 1, command_namepart},
        {"owner", 1, command_owner}
    };

    printf("I'm waiting for commands:\n");

    while(!quitFlag){
        scanf("%s", command);
        fgets(modifiers, MAXCOMMANDLEN+2, stdin);

        if(strlen(command) > MAXCOMMANDLEN || strlen(modifiers) > MAXCOMMANDLEN){
            fprintf(stderr, "COMMAND IS TOO LONG\n");
            while(getchar() != '\n'); //WE HAVE TO DISCARD ALL PENDING CHARACTERS
            if(!memset(command, '\0', MAXCOMMANDLEN+2)) ERR("memset");
            if(!memset(modifiers, '\0', MAXCOMMANDLEN+2)) ERR("memset");
            continue;
        }

        if((newline = strrchr(modifiers, '\n'))) *newline='\0';

        modifiers_length = strlen(modifiers);
        begin_of_modifiers = modifiers;
        if(modifiers_length > 0) { //WE HAVE SPACE AT THE BEGINNING OF STRING  
            modifiers_length -= 1; 
            begin_of_modifiers += 1; 
        }

        for(i=0; i<COMMANDSCOUNT; ++i){
            if(strcmp(command, commands[i].name) == 0){
                if((modifiers_length == 0 && !commands[i].allow_additional_args) || (modifiers_length > 0 && commands[i].allow_additional_args))
                    commands[i].command_fun(args, begin_of_modifiers);
                else
                    fprintf(stderr, "WRONG USAGE OF %s\n", command);  
                break;
            }
        }
        
        if(i == COMMANDSCOUNT)
            fprintf(stderr, "WRONG COMMAND\n");

        pthread_mutex_lock(&args->mx_quit_flag);
        quitFlag = args->quit_flag;
        pthread_mutex_unlock(&args->mx_quit_flag);

        if(!memset(command, '\0', MAXCOMMANDLEN+2)) ERR("memset");
        if(!memset(modifiers, '\0', MAXCOMMANDLEN+2)) ERR("memset");
    }
}

//---------------------COMMANDS-------------------------------------------------------

void command_exit(thread_args_t* args, char* mod){
    pthread_mutex_lock(&args->mx_quit_flag);
    args->quit_flag = 1;
    pthread_mutex_unlock(&args->mx_quit_flag);
}

void command_exitForced(thread_args_t* args, char* mod){
    command_exit(args, mod);

    pthread_mutex_lock(&args->mx_is_somebody_indexing);
    if(args->is_somebody_indexing) 
        pthread_cancel(args->indexer_tid);
    pthread_mutex_unlock(&args->mx_is_somebody_indexing);

    if(args->periodic_indexing_time!=0) pthread_cancel(args->manager_tid);
}

void command_index(thread_args_t* args, char* mod){
    pthread_mutex_lock(&args->mx_is_somebody_indexing);
    if(args->is_somebody_indexing) 
        fprintf(stderr, "INDEXING IS ALREADY RUNNING\n");
    else{
        args->is_somebody_indexing = 1;
        run_detached_indexing(args);
    }
    pthread_mutex_unlock(&args->mx_is_somebody_indexing);
}

void command_count(thread_args_t* args, char* mod){
    int i;
    int png=0, jpeg=0, zip=0, gzip=0, dir=0;
    
    pthread_mutex_lock(&args->mx_index);
    for(i=0; i<args->index.length; ++i){
        switch (args->index.elements[i].type) {
        case PNG: ++png; break;
        case JPEG: ++jpeg; break;
        case ZIP: ++zip; break;
        case GZIP: ++gzip; break;
        case DIR: ++dir; break;
        }
    }
    pthread_mutex_unlock(&args->mx_index);
    printf("+------+------------+\n");
    printf("| TYPE |    COUNT   |\n");
    printf("+------+------------+\n");
    printf("| PNG  | %10d |\n", png);
    printf("| JPEG | %10d |\n", jpeg);
    printf("| ZIP  | %10d |\n", zip);
    printf("| GZIP | %10d |\n", gzip);
    printf("| DIR  | %10d |\n", dir);
    printf("+------+------------+\n");
}

void command_namepart(thread_args_t* args, char* mod){
    int i;
    int_vector_t ids;
    init_vec(&ids);

    pthread_mutex_lock(&args->mx_index);
    for(i=0; i<args->index.length; ++i)
        if(strstr(args->index.elements[i].filename, mod))
            push_to_vec(&ids, i);
    pthread_mutex_unlock(&args->mx_index);

    show_result(args, &ids);
    delete_vec(&ids);
}

void command_largerthan(thread_args_t* args, char* mod){
    int i;
    int_vector_t ids;

    errno = 0;
    long val = strtol(mod, NULL, 10);
    if(errno == EINVAL || errno == ERANGE || !is_number(mod)){
        fprintf(stderr, "WRONG USAGE OF largerthan\n");
        return;
    }
    
    init_vec(&ids);

    pthread_mutex_lock(&args->mx_index);
    for(i=0; i<args->index.length; ++i)
        if(args->index.elements[i].size > val)
            push_to_vec(&ids, i);
    pthread_mutex_unlock(&args->mx_index);

    show_result(args, &ids);
    delete_vec(&ids);
}

void command_owner(thread_args_t* args, char* mod){
    int i;
    int_vector_t ids;

    errno = 0;
    uid_t owner = strtoul(mod, NULL, 10);
    if(errno == EINVAL || errno == ERANGE || !is_number(mod)){
        fprintf(stderr, "WRONG USAGE OF owner\n");
        return;
    }
    
    init_vec(&ids);

    pthread_mutex_lock(&args->mx_index);
    for(i=0; i<args->index.length; ++i){
        if(args->index.elements[i].ownerUID == owner)
            push_to_vec(&ids, i);
    }
    pthread_mutex_unlock(&args->mx_index);

    show_result(args, &ids);
    delete_vec(&ids);
}

//------------------------SHOWING DATA------------------------------------

void show_result(thread_args_t*args, int_vector_t* ids){
    int i, j;
    char* pager = getenv("PAGER");
    FILE* stream = stdout;

    char* types[FILETYPECOUNT] = {
        "PNG", 
        "JPEG",
        "GZIP",
        "ZIP",
        "DIR" 
    };

    pthread_mutex_lock(&args->mx_index);
    if(ids->length > 3 && pager) 
        if((stream = popen(pager, "w"))<0) ERR("PAGER");

    for(i=0; i<ids->length; ++i){
        j = ids->elements[i];
        fprintf(stream, "%s %ld %s \n", args->index.elements[j].full_path, args->index.elements[j].size, types[args->index.elements[j].type]);
        if(ferror(stream))
            break;
    }

    if(ids->length > 3 && pager)
        if(pclose(stream)) ERR("pclose");
    pthread_mutex_unlock(&args->mx_index);
}

//------------------------INT VECTOR MANIPULATION-----------------------------

void init_vec(int_vector_t* vec){
    vec->length = 0;
    vec->max_size = VECTORINITSIZE;
    vec->elements = malloc(sizeof(int)*vec->max_size);
    if(!vec->elements) ERR("malloc");
}

void expand_vec(int_vector_t* vec){
    vec->max_size *= 2;
    int* tmp = realloc(vec->elements, sizeof(int)*vec->max_size);
    if(!tmp){
        free(vec->elements);
        ERR("realloc");
    }
    vec->elements = tmp;
}

void push_to_vec(int_vector_t* vec, int elem){
    if(vec->length == vec->max_size) 
        expand_vec(vec);

    vec->elements[vec->length] = elem;
    ++(vec->length);
}

void delete_vec(int_vector_t* vec){
    free(vec->elements);
}

//---------------------------------------------------------------------------

int is_number(char* s){
    while(*s != '\0'){
        if(!isdigit(*s)) 
            return 0;
        s += 1;
    }
    return 1;
}