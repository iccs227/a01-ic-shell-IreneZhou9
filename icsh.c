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
#include <sys/types.h>   // pid_t
#include <sys/wait.h>    // waitpid(), WIFEXITED, WNOHANG
#include <signal.h>      // signal handling
#include <fcntl.h>       // open(), O_RDONLY, O_CREAT, etc.

#define BUFSIZE 1024
#define MAXJOBS 128

// job struct for background jobs
typedef struct {
    int    jid;          // job ID [1] [2] etc
    pid_t  pid;          // process ID
    int    state;        // Job state: RUNNING, STOPPED
    char   *cmdline;     // command line
} job_t;

// Job states
#define UNDEF 0    // undefined
#define RUNNING 1   // running
#define STOPPED 2   // stopped

static job_t jobs[MAXJOBS];  // job list
static int nextjid = 1;      // next job ID to allocate
static int fg_pid = 0;       // current foreground job pid

// Store the current command being executed
static char current_cmd[BUFSIZE];

// Function prototypes
static void initjobs(job_t *jobs);
static int addjob(job_t *jobs, pid_t pid, int state, char *cmdline);
static int deletejob(job_t *jobs, pid_t pid);
static job_t *getjobpid(job_t *jobs, pid_t pid);
static job_t *getjobjid(job_t *jobs, int jid);

// Initialize the job list
static void initjobs(job_t *jobs) {
    for (int i = 0; i < MAXJOBS; i++) {
        jobs[i].jid = 0;
        jobs[i].pid = 0;
        jobs[i].state = UNDEF;
        jobs[i].cmdline = NULL;
    }
}

// Add a job to the job list
static int addjob(job_t *jobs, pid_t pid, int state, char *cmdline) {
    if (pid < 1) return 0;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            jobs[i].cmdline = strdup(cmdline);
            return 1;
        }
    }
    return 0;  // job list is full
}

// Delete a job from the job list
static int deletejob(job_t *jobs, pid_t pid) {
    if (pid < 1) return 0;
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            free(jobs[i].cmdline);
            jobs[i].pid = 0;
            jobs[i].jid = 0;
            jobs[i].state = UNDEF;
            jobs[i].cmdline = NULL;
            return 1;
        }
    }
    return 0;
}

// Find a job by pid
static job_t *getjobpid(job_t *jobs, pid_t pid) {
    if (pid < 1) return NULL;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

// Find a job by jid
static job_t *getjobjid(job_t *jobs, int jid) {
    if (jid < 1) return NULL;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

// Signal handlers
void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job_t *job = getjobpid(jobs, pid);
        if (!job) continue;
        
        if (WIFSTOPPED(status)) {
            job->state = STOPPED;
            char msg[100];
            int len = snprintf(msg, sizeof(msg), "\n[%d]+ Stopped                 %s\n", job->jid, job->cmdline);
            write(STDOUT_FILENO, msg, len);
            if (pid == fg_pid) {
                fg_pid = 0;
                write(STDOUT_FILENO, "icsh $ ", 7);
            }
        }
        else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (job->state == RUNNING && !fg_pid) {  // Only print for background jobs
                char msg[100];
                int len = snprintf(msg, sizeof(msg), "[%d]+ Done                    %s\n", job->jid, job->cmdline);
                write(STDOUT_FILENO, msg, len);
            }
            deletejob(jobs, pid);
            if (pid == fg_pid) {
                fg_pid = 0;
            }
        }
    }
}

void handle_sigtstp(int sig) {
    if (fg_pid > 0) {
        kill(-fg_pid, SIGTSTP);  // Send SIGTSTP to the entire process group
    }
}

void handle_sigint(int sig) {
    if (fg_pid > 0) {
        kill(-fg_pid, SIGINT);  // Send SIGINT to the entire process group
    } else {
        write(STDOUT_FILENO, "\nicsh $ ", 8);
    }
}

int main(int argc, char *argv[]) {
    // Initialize signal handlers
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);
    signal(SIGCHLD, handle_sigchld);
    
    // Initialize job list
    initjobs(jobs);
    
    FILE *in = stdin;
    bool script = false;
    int last_status = 0;

    char line[BUFSIZE];
    char last_cmd[BUFSIZE] = "";   // store last non-!! command
    char *cmd;                     // pointer to the current command string

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

        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines
        if (line[0] == '\0') {
            last_status = 0;
            continue;
        }

        // "!!": repeat last command, but only echo it in interactive mode
        if (strcmp(line, "!!") == 0) {
            if (last_cmd[0] == '\0') {
                last_status = 0;
                continue;
            }
            if (!script) {                // suppress print when in script
                printf("%s\n", last_cmd);
            }
            cmd = last_cmd;
            strncpy(current_cmd, last_cmd, BUFSIZE-1);
            current_cmd[BUFSIZE-1] = '\0';
        } else {
            strncpy(last_cmd, line, BUFSIZE-1);
            last_cmd[BUFSIZE-1] = '\0';
            cmd = line;
            strncpy(current_cmd, line, BUFSIZE-1);
            current_cmd[BUFSIZE-1] = '\0';
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
            char *arg = cmd + 4;
            while (*arg == ' ') arg++;  // skip spaces
            if (*arg != '\0') {
                code = atoi(arg) & 0xFF;
            }
            if (!script) {
                printf("bye\n");
            }
            // Kill all remaining jobs before exiting
            for (int i = 0; i < MAXJOBS; i++) {
                if (jobs[i].pid != 0) {
                    kill(-jobs[i].pid, SIGKILL);
                    waitpid(jobs[i].pid, NULL, 0);  // Wait for the process to die
                    deletejob(jobs, jobs[i].pid);
                }
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

        // Built-in: jobs
        if (strcmp(cmd, "jobs") == 0) {
            for (int i = 0; i < MAXJOBS; i++) {
                if (jobs[i].pid != 0) {
                    printf("[%d]%c %-20s %s%s\n", 
                        jobs[i].jid,
                        (jobs[i].jid == nextjid-1) ? '+' : '-',
                        jobs[i].state == RUNNING ? "Running" : "Stopped",
                        jobs[i].cmdline,
                        jobs[i].state == RUNNING ? " &" : "");
                }
            }
            last_status = 0; 
            continue;
        }

        // Built-in: bg %<job_id> — resume a stopped job in the background
        if (strncmp(cmd, "bg", 2) == 0 &&
            (cmd[2] == ' ' || cmd[2] == '\0'))
        {
            char *arg = cmd + 2;         // skip "bg"
            while (*arg == ' ') arg++;   // skip spaces
            if (*arg == '\0') {
                fprintf(stderr, "bg: usage: bg %%<job_id>\n");
                last_status = 1;
                continue;
            }
            if (*arg == '%') arg++;      // skip the '%'
            int jid = atoi(arg);         // job-id to resume
            if (jid <= 0) {
                fprintf(stderr, "bg: invalid job id: %s\n", arg);
                last_status = 1;
                continue;
            }
            job_t *job = getjobjid(jobs, jid);
            if (!job) {
                fprintf(stderr, "bg: %%%d: no such job\n", jid);
                last_status = 1;
                continue;
            }
            
            if (kill(-job->pid, SIGCONT) < 0) {
                perror("kill (SIGCONT)");
                last_status = 1;
                continue;
            }
            
            job->state = RUNNING;
            printf("[%d]+ %s &\n", job->jid, job->cmdline);
            continue;
        }

        // Built-in: fg %<job_id> — bring a background job to foreground
        if (strncmp(cmd, "fg", 2) == 0 &&
            (cmd[2] == ' ' || cmd[2] == '\0'))
        {
            char *arg = cmd + 2;         // skip "fg"
            while (*arg == ' ') arg++;   // skip spaces
            if (*arg == '\0') {
                fprintf(stderr, "fg: usage: fg %%<job_id>\n");
                last_status = 1;
                continue;
            }
            if (*arg == '%') arg++;      // skip the '%'
            int jid = atoi(arg);         // job-id to foreground
            job_t *job = getjobjid(jobs, jid);
            if (!job) {
                fprintf(stderr, "fg: %%%d: no such job\n", jid);
                last_status = 1;
                continue;
            }
            
            // Store command before potentially removing job
            char cmd_copy[BUFSIZE];
            strncpy(cmd_copy, job->cmdline, BUFSIZE-1);
            cmd_copy[BUFSIZE-1] = '\0';
            
            if (kill(-job->pid, SIGCONT) < 0) {
                perror("kill (SIGCONT)");
                last_status = 1;
                continue;
            }
            
            job->state = RUNNING;
            fg_pid = job->pid;
            printf("%s\n", cmd_copy);
            
            int status;
            if (waitpid(job->pid, &status, WUNTRACED) < 0) {
                if (errno != ECHILD) {  // Ignore "No child processes" error
                    perror("waitpid");
                }
            }
            
            if (WIFSTOPPED(status)) {
                job->state = STOPPED;
            } else {
                deletejob(jobs, job->pid);
                fg_pid = 0;
            }
            continue;
        }

        // Handle regular command execution
        else {
            bool is_bg = false;
            size_t cmdlen = strlen(cmd);
            if (cmdlen > 0 && cmd[cmdlen-1] == '&') {
                is_bg = true;
                cmd[cmdlen-1] = '\0';  // Remove &
                while (cmdlen > 1 && cmd[cmdlen-2] == ' ') {
                    cmd[cmdlen-2] = '\0';
                    cmdlen--;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                continue;
            }
            else if (pid == 0) {  // Child
                // Create new process group
                if (setpgid(0, 0) < 0) {
                    perror("setpgid");
                    exit(1);
                }
                
                // Reset signal handlers to default in child
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);
                signal(SIGCONT, SIG_DFL);
                signal(SIGTTIN, SIG_DFL);
                signal(SIGTTOU, SIG_DFL);
                
                // Parse command into arguments
                char *argv[BUFSIZE/2];
                int argc = 0;
                char *token = strtok(cmd, " \t");
                while (token && argc < (BUFSIZE/2 - 1)) {
                    argv[argc++] = token;
                    token = strtok(NULL, " \t");
                }
                argv[argc] = NULL;
                
                execvp(argv[0], argv);
                perror(argv[0]);  // Only reaches here if exec fails
                exit(1);
            }
            else {  // Parent
                // Put child in its own process group
                if (setpgid(pid, pid) < 0) {
                    // Ignore race condition error
                    if (errno != EACCES) {
                        perror("setpgid");
                    }
                }
                
                if (is_bg) {
                    // Add to job list and print job info
                    addjob(jobs, pid, RUNNING, cmd);
                    printf("[%d] %d\n", nextjid-1, pid);
                }
                else {
                    // Foreground process
                    fg_pid = pid;
                    strcpy(current_cmd, cmd);
                    
                    int status;
                    if (waitpid(pid, &status, WUNTRACED) < 0) {
                        if (errno != ECHILD) {  // Ignore "No child processes" error
                            perror("waitpid");
                        }
                    }
                    
                    if (WIFSTOPPED(status)) {
                        // Job was stopped (Ctrl+Z)
                        addjob(jobs, pid, STOPPED, cmd);
                    }
                    else {
                        // Job completed or was killed
                        fg_pid = 0;
                    }
                }
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
