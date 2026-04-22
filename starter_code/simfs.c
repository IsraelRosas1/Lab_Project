/* This program simulates a file system within an actual file. The
 * simulated file system has only one directory and two types of
 * metadata structures defined in simfstypes.h.
 */

/* Simfs is run as:
 * simfs -f myfs command args
 * where the -f option specifies the actual file that the simulated file system
 * will occupy, the command is the command to be run on the simulated file system,
 * and args represents the list of arguments for the command. Note that
 * different commands take different numbers of arguments.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "simfs.h"

// We use the ops array to match the file system command entered by the user.
#define MAXOPS  7
char *ops[MAXOPS] = {"initfs", "printfs", "createfile", "readfile",
                     "writefile", "deletefile", "info"};
int find_command(char *);

int
main(int argc, char **argv)
{
    int oc;       /* option character */
    char *cmd;    /* command to run on the file system */
    char *fsname; /* name of the simulated file system file */

    char *usage_string = "Usage: simfs -f file cmd arg1 arg2 ...\n";

    /* Get and check the number of arguments */
    if(argc < 4) {
        fputs(usage_string, stderr);
        exit(1);
    }

    while((oc = getopt(argc, argv, "f:")) != -1) {
        switch(oc) {
        case 'f' :
            fsname = optarg;
            break;
        default:
            fputs(usage_string, stderr);
            exit(1);
        }
    }

    /* Get the command name */
    cmd = argv[optind];
    optind++;

    switch((find_command(cmd))) {
    case 0: /* initfs */
        initfs(fsname);
        break;
    case 1: /* printfs */
        printfs(fsname);
        break;
    case 2: /* createfile */
        if (optind >= argc) {
            fprintf(stderr, "Usage: simfs -f file createfile <filename>\n");
            exit(1);
        }
        createfile(fsname, argv[optind]);
        break;
    case 3: /* readfile */
        if (optind + 2 >= argc) {
            fprintf(stderr, "Usage: simfs -f file readfile <filename> <start> <length>\n");
            exit(1);
        }
        char *read_name = argv[optind++];
        int read_start = atoi(argv[optind++]);
        int read_len = atoi(argv[optind++]);
        readfile(fsname, read_name, read_start, read_len);
        break;
    case 4: /* writefile */
        if (optind + 2 >= argc) {
            fprintf(stderr, "Usage: simfs -f file writefile <filename> <start> <length>\n");
            exit(1);
        }
        char *write_name = argv[optind++];
        int write_start = atoi(argv[optind++]);
        int write_len = atoi(argv[optind++]);
        writefile(fsname, write_name, write_start, write_len);
        break;
    case 5: /* deletefile */
        if (optind >= argc) {
            fprintf(stderr, "Usage: simfs -f file deletefile <filename>\n");
            exit(1);
        }
        deletefile(fsname, argv[optind]);
        break;
    default:
        fprintf(stderr, "Error: Invalid command\n");
        exit(1);
    }

    return 0;
}

/* Returns an integer corresponding to the file system command that
 * is going to be executed
 */
int
find_command(char *cmd)
{
    int i;
    for(i = 0; i < MAXOPS; i++) {
        if ((strncmp(cmd, ops[i], strlen(ops[i]))) == 0) {
            return i;
        }
    }
    fprintf(stderr, "Error: Command %s not found\n", cmd);
    return -1;
}
