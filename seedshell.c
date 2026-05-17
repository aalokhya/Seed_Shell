// SeedShell Complete - with IPC, Signals, Multithreading
// Compile: gcc seedshell_complete.c -o seedshell -lpthread
// Run:     ./seedshell

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_CMD_LEN 512
#define MAX_ARGS    20
#define MAX_PIPES   10

// ─────────────────────────────────────────────────
// SIGNAL HANDLING (Addition 1)
// ─────────────────────────────────────────────────

volatile sig_atomic_t child_pid = 0;  // track running child

void handle_sigint(int sig) {
    // If a child is running, kill only the child — not the shell
    if (child_pid > 0) {
        kill(child_pid, SIGINT);
    } else {
        // No child: just print a new prompt
        write(STDOUT_FILENO, "\nseedsh$ ", 9);
    }
}

void setup_signals() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // restart syscalls interrupted by signal
    sigaction(SIGINT, &sa, NULL);

    // Ignore SIGQUIT in shell (Ctrl+\)
    signal(SIGQUIT, SIG_IGN);
}

// ─────────────────────────────────────────────────
// MULTITHREADING DEMO (Addition 2)
// ─────────────────────────────────────────────────

// Shared counter — used to show race condition
int shared_counter = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int thread_id;
    int iterations;
    int use_mutex;   // 1 = safe, 0 = race condition demo
} ThreadArgs;

void *counter_thread(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    for (int i = 0; i < args->iterations; i++) {
        if (args->use_mutex) {
            pthread_mutex_lock(&counter_mutex);
            shared_counter++;
            pthread_mutex_unlock(&counter_mutex);
        } else {
            // Intentional race condition (no lock)
            shared_counter++;
        }
    }
    printf("[Thread %d] done. Counter so far: %d\n", args->thread_id, shared_counter);
    return NULL;
}

void seed_runthread(int use_mutex) {
    shared_counter = 0;
    int iterations = 100000;

    pthread_t t1, t2;
    ThreadArgs a1 = {1, iterations, use_mutex};
    ThreadArgs a2 = {2, iterations, use_mutex};

    if (use_mutex) {
        printf("Running 2 threads WITH mutex (safe)...\n");
    } else {
        printf("Running 2 threads WITHOUT mutex (race condition demo)...\n");
    }

    pthread_create(&t1, NULL, counter_thread, &a1);
    pthread_create(&t2, NULL, counter_thread, &a2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("Expected counter: %d\n", iterations * 2);
    printf("Actual  counter: %d\n", shared_counter);
    if (shared_counter != iterations * 2) {
        printf("RACE CONDITION detected! Values were lost.\n");
    } else {
        printf("Correct! Mutex protected the shared data.\n");
    }
}

// ─────────────────────────────────────────────────
// PIPE / IPC (Addition 3)
// ─────────────────────────────────────────────────

// Execute a single command with args, optionally with in/out fd redirection
void exec_cmd(char **args, int in_fd, int out_fd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect stdin/stdout if needed
        if (in_fd != STDIN_FILENO) {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        if (out_fd != STDOUT_FILENO) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }
        execvp(args[0], args);
        fprintf(stderr, "seedsh: %s: %s\n", args[0], strerror(errno));
        exit(1);
    } else if (pid < 0) {
        perror("fork");
    }
    // Parent does NOT wait here — caller handles it
}

// Run a pipeline: cmd1 | cmd2 | cmd3 ...
void run_pipeline(char *cmds[], int num_cmds) {
    int pipes[MAX_PIPES][2];   // pipe fds
    int pids[MAX_PIPES + 1];

    // Create all needed pipes
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return;
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        // Tokenize this individual command
        char *args[MAX_ARGS];
        int argc = 0;
        char *tok = strtok(cmds[i], " \t");
        while (tok && argc < MAX_ARGS - 1) {
            args[argc++] = tok;
            tok = strtok(NULL, " \t");
        }
        args[argc] = NULL;

        int in_fd  = (i == 0)            ? STDIN_FILENO  : pipes[i-1][0];
        int out_fd = (i == num_cmds - 1) ? STDOUT_FILENO : pipes[i][1];

        pid_t pid = fork();
        pids[i] = pid;

        if (pid == 0) {
            // Redirect
            if (in_fd != STDIN_FILENO)  { dup2(in_fd,  STDIN_FILENO);  close(in_fd); }
            if (out_fd != STDOUT_FILENO){ dup2(out_fd, STDOUT_FILENO); close(out_fd);}

            // Close all pipe fds in child (we only use the ones we dup2'd)
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(args[0], args);
            fprintf(stderr, "seedsh: %s: %s\n", args[0], strerror(errno));
            exit(1);
        } else if (pid < 0) {
            perror("fork");
        }
    }

    // Parent: close all pipe ends
    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all children
    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

// ─────────────────────────────────────────────────
// I/O REDIRECTION (>, >>, <)
// ─────────────────────────────────────────────────

// Returns 1 if redirection was handled, 0 if no redirection found
// Modifies args[] to remove redirection tokens
int handle_redirection(char **args, int *in_fd, int *out_fd) {
    int found = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0 && args[i+1]) {
            *out_fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*out_fd < 0) { perror("open"); return -1; }
            args[i] = NULL;  // cut the arg list here
            found = 1;
        } else if (strcmp(args[i], ">>") == 0 && args[i+1]) {
            *out_fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (*out_fd < 0) { perror("open"); return -1; }
            args[i] = NULL;
            found = 1;
        } else if (strcmp(args[i], "<") == 0 && args[i+1]) {
            *in_fd = open(args[i+1], O_RDONLY);
            if (*in_fd < 0) { perror("open"); return -1; }
            args[i] = NULL;
            found = 1;
        }
    }
    return found;
}

// ─────────────────────────────────────────────────
// ORIGINAL SEED COMMANDS (kept from your version)
// ─────────────────────────────────────────────────

void seed_help() {
    printf("\n=== SeedShell Commands ===\n\n");
    printf("Process & System:\n");
    printf("  seedpid          - Show shell process ID (uses getpid)\n");
    printf("  seedtime         - Current date/time (uses time/ctime)\n");
    printf("  seedwho          - Current user (uses getpwuid/getuid)\n");
    printf("  seedwait <secs>  - Sleep N seconds (uses sleep syscall)\n");
    printf("\nFile System:\n");
    printf("  seedlist         - List files (forks ls -l)\n");
    printf("  seedmake <dir>   - Create directory (uses mkdir syscall)\n");
    printf("  seedsay <msg>    - Print message\n");
    printf("  seedadd <a> <b>  - Add two numbers\n");
    printf("\nIPC & Signals:\n");
    printf("  seedpipe         - Demo: ls | grep .c (pipe + dup2)\n");
    printf("  seedsiginfo      - Show active signal handlers\n");
    printf("\nMultithreading:\n");
    printf("  seedrunthread    - 2 threads WITH mutex (correct)\n");
    printf("  seedrace         - 2 threads WITHOUT mutex (race condition)\n");
    printf("\nRedirection (built-in):\n");
    printf("  cmd > file       - Redirect stdout to file\n");
    printf("  cmd >> file      - Append stdout to file\n");
    printf("  cmd < file       - Redirect stdin from file\n");
    printf("  cmd1 | cmd2      - Pipe output of cmd1 to cmd2\n");
    printf("\n  exit            - Exit SeedShell\n\n");
}

void seed_pid()  { printf("SeedShell PID: %d\n", getpid()); }

void seed_time() {
    time_t now = time(NULL);
    printf("Current date & time: %s", ctime(&now));
}

void seed_list() {
    pid_t pid = fork();
    if (pid == 0) { execlp("ls", "ls", "-l", "--color=auto", NULL); exit(1); }
    else { child_pid = pid; waitpid(pid, NULL, 0); child_pid = 0; }
}

void seed_make(char *dir) {
    if (!dir) { printf("Usage: seedmake <dir>\n"); return; }
    if (mkdir(dir, 0755) == 0) printf("Directory '%s' created.\n", dir);
    else perror("mkdir");
}

void seed_say(char *msg) {
    if (!msg) { printf("Usage: seedsay <message>\n"); return; }
    printf("SeedShell says: %s\n", msg);
}

void seed_who() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) printf("Current user: %s\n", pw->pw_name);
    else perror("getpwuid");
}

void seed_wait_cmd(char *s) {
    if (!s) { printf("Usage: seedwait <seconds>\n"); return; }
    int secs = atoi(s);
    if (secs <= 0) { printf("Enter a positive number.\n"); return; }
    printf("Waiting %d seconds...\n", secs);
    sleep(secs);
    printf("Done.\n");
}

void seed_add(char *a, char *b) {
    if (!a || !b) { printf("Usage: seedadd <num1> <num2>\n"); return; }
    printf("Result: %d + %d = %d\n", atoi(a), atoi(b), atoi(a)+atoi(b));
}

void seed_siginfo() {
    printf("Signal handlers active in SeedShell:\n");
    printf("  SIGINT  (Ctrl+C) -> custom handler: kills child, keeps shell alive\n");
    printf("  SIGQUIT (Ctrl+\\) -> ignored by shell\n");
    printf("  SIGCHLD          -> default (reap children)\n");
    printf("Shell PID: %d\n", getpid());
}

void seed_pipe_demo() {
    printf("Running: ls | grep .c\n");
    char *cmds[] = {"ls", "grep .c"};
    run_pipeline(cmds, 2);
}

// ─────────────────────────────────────────────────
// COMMAND PARSING & MAIN LOOP
// ─────────────────────────────────────────────────

int main() {
    char input[MAX_CMD_LEN];
    char *args[MAX_ARGS];

    setup_signals();

    printf("Welcome to SeedShell!\n");
    printf("Type 'seedhelp' for all commands.\n");
    printf("Supports: pipes (|), redirection (>, >>, <), signals, threads.\n\n");

    while (1) {
        printf("seedsh$ ");
        fflush(stdout);

        if (fgets(input, MAX_CMD_LEN, stdin) == NULL) {
            printf("\nGoodbye!\n");
            break;
        }

        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        // ── Check for pipe  ──────────────────────────────────
        if (strchr(input, '|')) {
            char *cmds[MAX_PIPES];
            int num = 0;
            char *segment = strtok(input, "|");
            while (segment && num < MAX_PIPES) {
                // trim leading space
                while (*segment == ' ') segment++;
                cmds[num++] = segment;
                segment = strtok(NULL, "|");
            }
            run_pipeline(cmds, num);
            continue;
        }

        // ── Tokenize  ────────────────────────────────────────
        int argc = 0;
        char *tok = strtok(input, " \t");
        while (tok && argc < MAX_ARGS - 1) {
            args[argc++] = tok;
            tok = strtok(NULL, " \t");
        }
        args[argc] = NULL;
        if (argc == 0) continue;

        // ── Built-ins  ───────────────────────────────────────
        if (strcmp(args[0], "exit") == 0) { printf("Goodbye!\n"); break; }

        if (strcmp(args[0], "cd") == 0) {
            if (!args[1]) fprintf(stderr, "cd: missing argument\n");
            else if (chdir(args[1]) != 0) perror("cd");
            continue;
        }

        // ── Seed custom commands  ─────────────────────────────
        if (strncmp(args[0], "seed", 4) == 0) {
            if      (!strcmp(args[0], "seedhelp"))      seed_help();
            else if (!strcmp(args[0], "seedpid"))       seed_pid();
            else if (!strcmp(args[0], "seedtime"))      seed_time();
            else if (!strcmp(args[0], "seedlist"))      seed_list();
            else if (!strcmp(args[0], "seedmake"))      seed_make(args[1]);
            else if (!strcmp(args[0], "seedsay"))       seed_say(args[1]);
            else if (!strcmp(args[0], "seedwho"))       seed_who();
            else if (!strcmp(args[0], "seedwait"))      seed_wait_cmd(args[1]);
            else if (!strcmp(args[0], "seedadd"))       seed_add(args[1], args[2]);
            else if (!strcmp(args[0], "seedpipe"))      seed_pipe_demo();
            else if (!strcmp(args[0], "seedsiginfo"))   seed_siginfo();
            else if (!strcmp(args[0], "seedrunthread")) seed_runthread(1);
            else if (!strcmp(args[0], "seedrace"))      seed_runthread(0);
            else printf("Unknown seed command: %s (try seedhelp)\n", args[0]);
            continue;
        }

        // ── External command with optional redirection  ───────
        int in_fd  = STDIN_FILENO;
        int out_fd = STDOUT_FILENO;
        handle_redirection(args, &in_fd, &out_fd);

        pid_t pid = fork();
        if (pid == 0) {
            if (in_fd  != STDIN_FILENO)  { dup2(in_fd,  STDIN_FILENO);  close(in_fd); }
            if (out_fd != STDOUT_FILENO) { dup2(out_fd, STDOUT_FILENO); close(out_fd);}
            execvp(args[0], args);
            fprintf(stderr, "seedsh: %s: command not found\n", args[0]);
            exit(1);
        } else if (pid > 0) {
            child_pid = pid;
            waitpid(pid, NULL, 0);
            child_pid = 0;
            // Close redirected fds in parent
            if (in_fd  != STDIN_FILENO)  close(in_fd);
            if (out_fd != STDOUT_FILENO) close(out_fd);
        } else {
            perror("fork");
        }
    }
    return 0;
}
