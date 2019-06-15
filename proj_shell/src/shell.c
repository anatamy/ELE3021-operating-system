/*
 * Operating system 2019
 * Professor: Hyungsoo Jung
 * Student NO: 2014018940
 * Student Name: Hyung-Kwon Ko
 * Since: 2019-03-22
 * Version: 0.0.0
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>

#define BUFFER_SIZE     50

/*
 * Usage: to get line 
 * @param[in]   input - input string
 * @param[in]   n - necessity for getline function
 * @param[in]   bsize - buffer size for dynamic allocation
 * @param[out]  input - processed input string
 */
char *get_line(char *input, size_t n, int* bsize) {
    input = NULL;
    size_t r = getline(&input, &n, stdin);
    if(r <= 1) {
        return NULL;
    }
    while(r > *bsize) {
        (*bsize) *= 2;
        input = realloc(input, *bsize);
    }
    input[strlen(input) - 1] = 0;
    return input;
}

/*
 * Usage: to trim whitespaces
 * @param[in]   str - string that you want to remove whitespaces
 */
void trim(char *str) {
    int ix = 0;
    int i = 0;

    while(str[ix] == ' ' || str[ix] == '\t' || str[ix] == '\n') {
        ix++;
    }

    while(str[i + ix] != '\0') {
        str[i] = str[i + ix];
        i++;
    }
    str[i] = '\0';

    i = 0;
    ix = -1;
    while(str[i] != '\0') {
        if(str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
            ix = i;
        }
        i++;
    }
    str[ix + 1] = '\0';
}

/*
 * Usage: to parse a single command by the space ' '
 * @param[in]   input - single command
 * @param[in]   bsize - size of a command for dynamic allocation
 * @param[out]  command - tokenized command(pointer array)
 */
char **get_parsed(char *input, int *bsize) {
    char **command = malloc((*bsize)*sizeof(char *));
    char *separator = " \t\n";
    char *parsed;
    int ix = 0;
    parsed = strtok(input, separator);
    while(parsed != NULL) {
        command[ix++] = parsed;
        parsed = strtok(NULL, separator);
    }
    command[ix] = NULL;
    return command;
}

/*
 * Usage: to parse a single line into multiple commands with ';'
 * @param[in]   input - a single line
 * @param[in]   bsize - size of a command for dynamic allocation
 * @param[in]   ix - to hand over the number of commands in a single line
 * @param[out]  command - tokenized command(pointer array)
 */
char **get_parsed2(char *input, int *bsize, int *ix) {
    char **command = malloc((*bsize)*sizeof(char *));
    char *separator = ";";
    char *parsed;
    parsed = strtok(input, separator);
    while(parsed != NULL) {
        command[(*ix)++] = parsed;
        parsed = strtok(NULL, separator);
    }
    command[*ix] = NULL;

    for(int i =0; i<(*ix); i++) {
        trim(command[i]);
    }
    return command;
}

/*
 * Usage: to fork and exec process
 * @param[in]   cmd0 - command[0] (first part of command, e.g. ls)
 * @param[in]   cmd - command (double pointer)
 */
void myfork(char *cmd0, char **cmd) {
    int s, t;
    int pid = fork();
   
    // error handling (failed fork)
    if(pid < 0) {
        fprintf(stderr, "fork failed\n");
        exit(1);
    }

    // execute
    if(pid == 0) {
        t = execvp(cmd0, cmd);       
        if(t < 0) {
            printf("execution failed\n");
            exit(0);
        }
        fprintf(stderr, "this should never be seen\n");
        exit(1);            
    }
    wait(&s);
}

/* MAIN FUNCTION */
int main(int argc, char *argv[]) {

    // declarations
    char **command, **command2;
    char *input;
    int bsize = BUFFER_SIZE;
    int ix = 0;
    int i = 0;
    size_t n;

    // this will be the batch mode.
    // more than two inputs wlll just be ignored.
    if(argc > 1) {
        FILE *fp;
        char *line = NULL;
        ssize_t read;
        
        char a[20] = "./";
        strcat(a, argv[1]);
        fp = fopen(a, "r");

        // error handling. if fp == NULL, program exits.
        if(fp == NULL) {
            exit(EXIT_FAILURE);
        }

        while((read = getline(&line, &n, fp)) != -1) {
            line[strlen(line) - 1] = 0;
            ix = 0;
            bsize = 50;
            command2 = get_parsed2(line, &bsize, &ix);
            for(i=0; i<ix; i++) {
                printf("===========================\n");
                command = get_parsed(command2[i], &bsize);
                myfork(command[0], command);
                free(command);
            }
            free(command2);
        }
        //free(command);
        //free(command2);
        exit(0);
    }

    // interactive mode
    while(1) {
        ix = 0;
        bsize = 50;
        printf("myshell> ");

        // get a single line command
        input = get_line(input, n, &bsize);
       
        // to handle enter(no command)
        if(input == NULL) {
            continue;
        }

        // parse the command with ';' (if exists)
        command2 = get_parsed2(input, &bsize, &ix);
        for(i=0; i < ix; i++) {
            // parse the command with ' '(if exists)
            command = get_parsed(command2[i], &bsize);
            
            // buffer size back to original in case of expansion
            bsize = BUFFER_SIZE;
            
            // quit command
            if(strcmp(command[0], "quit") == 0) {
                exit(0);
            }

            // cd command
            if(strcmp(command[0], "cd") == 0) {
                if(command[1] == NULL) {
                    chdir("/");
                    continue;
                }

                // error handling
                if(chdir(command[1]) < 0) {
                    fprintf(stderr, "check the cd param\n");
                }
                continue;
            }

            // forked process will execute the command
            myfork(command[0], command);

            // free memory
            free(command);
        }
        free(command2);
        free(input);
    }
    return 0;
}
