/* ICCS227: Project 1: icsh
 * Name: Qi Zhou
 * StudentID: u6580536
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // for chdir()
#include <limits.h>    // for PATH_MAX
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>   // for pid_t
#include <sys/wait.h>    // for waitpid()
#include <signal.h> 
#include <fcntl.h> 


#define BUFSIZE 1024

// track the current foreground child PID
static pid_t fg_pid = 0;

// Milestone 4: SIGINT handler (Ctrl-C)
void handle_sigint(int sig) {
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    }
}

// Milestone 4: SIGTSTP handler (Ctrl-Z)
void handle_sigtstp(int sig) {
    if (fg_pid > 0) {
        kill(fg_pid, SIGTSTP);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT,  handle_sigint);
    signal(SIGTSTP, handle_sigtstp);

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

        // Milestone 3-5: external command handling + I/O redirection + signals
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

            // Milestone 5: detect < and >, remove them
            char *in_file = NULL, *out_file = NULL;
            for (int i = 0; i < argc_exec; i++) {
                if (strcmp(argv_exec[i], "<") == 0 && i+1 < argc_exec) {
                    in_file = argv_exec[i+1];
                    memmove(&argv_exec[i], &argv_exec[i+2],
                            sizeof(char*)*(argc_exec - i - 1));
                    argc_exec -= 2;  i--;
                }
                else if (strcmp(argv_exec[i], ">") == 0 && i+1 < argc_exec) {
                    out_file = argv_exec[i+1];
                    memmove(&argv_exec[i], &argv_exec[i+2],
                            sizeof(char*)*(argc_exec - i - 1));
                            argc_exec -= 2;  i--;
                }
            }
            argv_exec[argc_exec] = NULL;

            pid_t pid = fork();
            if (pid < 0) {
                // fork failed
                perror("fork");
                last_status = 1;
            }
            else if (pid == 0) {
                // child resets signals to default
                signal(SIGINT,  SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                // Milestone 5: input redirection
                if (in_file) {
                    int fd = open(in_file, O_RDONLY);
                    if (fd < 0) { perror(in_file); exit(1); }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                // Milestone 5: output redirection
                if (out_file) {
                    int fd = open(out_file,
                                  O_WRONLY|O_CREAT|O_TRUNC, 0666);
                    if (fd < 0) { perror(out_file); exit(1); }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                execvp(argv_exec[0], argv_exec);
                perror(argv_exec[0]);
                exit(1);
            }
            else {
                // Milestone 4: record & wait for foreground job
                fg_pid = pid;  
                int wstat;
                if (waitpid(pid, &wstat, WUNTRACED) == -1) {
                    perror("waitpid");
                    last_status = 1;
                }
                else if (WIFEXITED(wstat)) {
                    last_status = WEXITSTATUS(wstat);
                }
                else if (WIFSIGNALED(wstat)) {
                    // child was killed by a signal (e.g. SIGINT)
                    last_status = 128 + WTERMSIG(wstat);
                }
                else if (WIFSTOPPED(wstat)) {
                    last_status = 128 + WSTOPSIG(wstat);
                }
                fg_pid = 0;    // clear foreground pid
            }
        }
        continue;
    }

    // end-of-file reached
    // exit with last command's status in script mode
    if (script) exit(last_status);

    // interactive EOF
    printf("\n");
    return 0;
}
