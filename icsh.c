/* ICCS227: Project 1: icsh
 * Name: Qi Zhou
 * StudentID: u6580536
 */

/* icsh.c – Milestone 1 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // for chdir()
#include <limits.h>    // for PATH_MAX
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>   // for pid_t
#include <sys/wait.h>    // for waitpid()


#define BUFSIZE 1024

int main(int argc, char *argv[]) {
    FILE *in = stdin;
    bool script = false;
    int last_status = 0;

    char line[BUFSIZE];
    char last_cmd[BUFSIZE] = "";   // store last non-!! command

    // --- Milestone 2: detect script mode ---
    if (argc == 2) {
        // interpret argv[1] as script file: open for reading
        script = true;
        in     = fopen(argv[1], "r"); 
        if (!in) {
            fprintf(stderr, "icsh: cannot open %s: %s\n",
                    argv[1], strerror(errno));
            exit(1);
        }
    } else if (argc > 2) {
        // too many arguments: show usage and exit
        fprintf(stderr, "Usage: icsh [script-file]\n");
        exit(1);
    }

    // Main REPL / script loop
    while (1) {
        // prompt only in interactive mode
        if (!script) {
            printf("icsh $ ");
            fflush(stdout);
        }

        // read next line
        if (!fgets(line, BUFSIZE, in))
            break;                // EOF

        // Strip newline
        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines
        if (line[0] == '\0') {
            last_status = 0;
            continue;
        }

        char *cmd = line;

        // “!!”: repeat last command, but only echo it in interactive mode
        if (strcmp(line, "!!") == 0) {
            if (last_cmd[0] == '\0') {
                last_status = 0;
                continue;
            }
            if (!script) {                // suppress print when in script
                printf("%s\n", last_cmd);
            }
            cmd = last_cmd;
        } else {
            strncpy(last_cmd, line, BUFSIZE-1);
            last_cmd[BUFSIZE-1] = '\0';
        }



        // Built-in: echo
        if (strncmp(cmd, "echo", 4) == 0 &&
            (cmd[4]==' ' || cmd[4]=='\0'))
        {
            char *arg = cmd + 4;
            while (*arg == ' ') arg++;
            if (strcmp(arg, "$?") == 0) {
                printf("%d\n", last_status);
            } else {
                printf("%s\n", arg);
            }
            last_status = 0;
            continue;
        }

        // Built-in: exit
        if (strncmp(cmd, "exit", 4) == 0 &&
            (cmd[4] == ' ' || cmd[4] == '\0')) 
        {
            int code = 0;
            if (cmd[4] == ' ') 
                code = atoi(cmd + 5) & 0xFF;
            if (!script) {
                    printf("bye\n");
            }
            exit(code);
        }

        // Built-in: help
        if (strcmp(cmd, "help") == 0) {
            puts("Built-in commands:");
            puts("  echo <text>   print text");
            puts("  !!            repeat last command");
            puts("  exit [n]      exit shell with code n");
            puts("  cd [dir]      change directory");
            puts("  help          show this help");
            last_status = 0;
            continue;
        }

        // Built-in: cd
        if (strncmp(cmd, "cd", 2) == 0 &&
            (cmd[2] == ' ' || cmd[2] == '\0'))
        {
            char *path = cmd + 2;
            while (*path==' ') path++;
            if (*path=='\0') {
                path = getenv("HOME");
                if (!path) path = "/";
            }
            if (chdir(path)!=0) {
                fprintf(stderr, "cd: %s: %s\n",
                        path, strerror(errno));
                last_status = 1;
            } else {
                last_status = 0;
            }
            continue;
        }

        // Milestone 3: external command handling
        {
            // Split cmd into argv array
            char *argv_exec[BUFSIZE/2];
            int argc_exec = 0;
            char *token = strtok(cmd, " ");
            while (token && argc_exec < (BUFSIZE/2 - 1)) {
                argv_exec[argc_exec++] = token;
                token = strtok(NULL, " ");
            }
            argv_exec[argc_exec] = NULL;

            pid_t pid = fork();
            if (pid < 0) {
                // fork failed
                perror("fork");
                last_status = 1;
            }
            else if (pid == 0) {
                // child: execute external program
                execvp(argv_exec[0], argv_exec);
                // if execvp returns, there was an error
                perror(argv_exec[0]);  // e.g. "ls: No such file or directory"
                exit(1);
            }
            else {
                // parent: wait for child to finish
                int wstatus;
                if (waitpid(pid, &wstatus, 0) == -1) {
                    perror("waitpid");
                    last_status = 1;
                } else if (WIFEXITED(wstatus)) {
                    last_status = WEXITSTATUS(wstatus);
                } else {
                    last_status = 1;
                }
            }
        }
        continue;  // after external, loop back
    }

    // end-of-file reached
    // exit with last command's status in script mode
    if (script) exit(last_status);

    // interactive EOF
    printf("\n");
    return 0;
}
