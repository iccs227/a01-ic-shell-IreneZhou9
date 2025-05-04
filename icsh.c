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

#define BUFSIZE 1024

int main(void) {
    char line[BUFSIZE];
    char last[BUFSIZE] = "";   // store last non-!! command

    while (1) {
        // Prompt
        printf("icsh $ ");
        fflush(stdout);

        // Read input
        if (fgets(line, BUFSIZE, stdin) == NULL) {
            printf("\n");
            exit(0);
        }

        // Strip newline
        line[strcspn(line, "\n")] = '\0';

        // Skip empty
        if (line[0] == '\0') {
            continue;
        }

        char *cmd = line;

        // Repeat last: !!
        if (strcmp(line, "!!") == 0) {
            if (last[0] == '\0') {
                continue;  // no history
            }
            printf("%s\n", last);
            cmd = last;
        } else {
            // save history
            strncpy(last, line, BUFSIZE - 1);
            last[BUFSIZE - 1] = '\0';
        }

        // Built-in: echo
        if (strncmp(cmd, "echo ", 5) == 0) {
            printf("%s\n", cmd + 5);
            continue;
        }

        // Built-in: exit
        if (strncmp(cmd, "exit", 4) == 0 &&
            (cmd[4] == ' ' || cmd[4] == '\0')) {
            int code = 0;
            if (cmd[4] == ' ') {
                code = atoi(cmd + 5) & 0xFF;
            }
            printf("bye\n");
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
            continue;
        }

        // Built-in: cd
        if (strncmp(cmd, "cd", 2) == 0 &&
            (cmd[2] == ' ' || cmd[2] == '\0')) {
            char *path;
            if (cmd[2] == ' ' && cmd[3] != '\0') {
                path = cmd + 3;
            } else {
                path = getenv("HOME");  // default to HOME
            }
            if (chdir(path) != 0) {
                fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
            }
            continue;
        }

        // Unknown
        printf("bad command\n");
    }

    return 0;
}
