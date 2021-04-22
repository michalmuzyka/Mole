#ifndef HEADER_DEFINES
#define HEADER_DEFINES

#include <stdio.h>
#include <stdlib.h>

#define MAXPATHLEN 200
#define MAXFILENAMELEN 50
#define VECTORINITSIZE 8

#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))			 

#endif