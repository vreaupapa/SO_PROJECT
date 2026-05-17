#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>

volatile sig_atomic_t keep_running = 1;

void handle_sigusr1(int sig) {
    // Mesaj structurat cu prefixul EVENT
    const char *msg = "EVENT: Un nou raport a fost adaugat in sistem!\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

void handle_sigint(int sig) {
    // Mesaj structurat cu prefixul STOP
    const char *msg = "STOP: Semnal SIGINT primit. Se pregateste inchiderea...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    keep_running = 0;
}

int main() {
    // 1. Verificare daca un alt monitor este deja pornit
    int fd_check = open(".monitor_pid", O_RDONLY);
    if (fd_check != -1) {
        char pid_buf[32] = {0};
        int bytes = read(fd_check, pid_buf, sizeof(pid_buf) - 1);
        if (bytes > 0) {
            int existing_pid = atoi(pid_buf);
            // kill cu semnalul 0 verifica daca procesul mai traieste
            if (kill(existing_pid, 0) == 0) {
                char err_msg[128];
                snprintf(err_msg, sizeof(err_msg), "ERROR: Un monitor ruleaza deja cu PID-ul %d\n", existing_pid);
                write(STDOUT_FILENO, err_msg, strlen(err_msg));
                close(fd_check);
                return 1; // Se termina imediat
            }
        }
        close(fd_check);
    }

    struct sigaction sa_usr1, sa_int;

    sa_usr1.sa_handler = handle_sigusr1;
    sa_usr1.sa_flags = 0;
    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);

    sa_int.sa_handler = handle_sigint;
    sa_int.sa_flags = 0;
    sigemptyset(&sa_int.sa_mask);
    sigaction(SIGINT, &sa_int, NULL);

    int fd = open(".monitor_pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Eroare la crearea fisierului .monitor_pid");
        return 1;
    }
    
    pid_t my_pid = getpid();
    char pid_str[32];
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", my_pid);
    write(fd, pid_str, len);
    close(fd);

    // Mesaj de pornire
    char start_msg[128];
    snprintf(start_msg, sizeof(start_msg), "START: Monitor activat cu PID %d.\n", my_pid);
    write(STDOUT_FILENO, start_msg, strlen(start_msg));

    while (keep_running) {
        pause();
    }

    unlink(".monitor_pid");
    return 0;
}