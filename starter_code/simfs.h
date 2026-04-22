#include <stdio.h>
#include "simfstypes.h"

/* File system operations */
void printfs(char *);
void initfs(char *);
void createfile(char *fsname, char *filename);
void deletefile(char *fsname, char *filename);
void readfile(char *fsname, char *filename, int start, int length);
void writefile(char *fsname, char *filename, int start, int length);
/* Internal functions */
FILE *openfs(char *filename, char *mode);
void closefs(FILE *fp);
