#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>

// Variabila globala folosita ca steag pentru oprirea programului.
// sig_atomic_t garanteaza ca citirea/scrierea ei nu este intrerupta de semnale.
volatile sig_atomic_t keep_running = 1;

// Handler pentru un raport nou
void handle_sigusr1(int sig) {
    const char *msg = "[MONITOR] Un nou raport a fost adaugat!\n";
    // Folosim write in loc de printf pentru ca este async-signal-safe
    write(STDOUT_FILENO, msg, strlen(msg));
}

// Handler pentru inchiderea programului (Ctrl+C)
void handle_sigint(int sig) {
    const char *msg = "\n[MONITOR] Semnal SIGINT primit. Se pregateste inchiderea...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    keep_running = 0; // Acest lucru va sparge bucla while din main
}

int main() {
    struct sigaction sa_usr1, sa_int;

    // 1. Configurare SIGUSR1
    sa_usr1.sa_handler = handle_sigusr1;
    sa_usr1.sa_flags = 0;
    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);

    // 2. Configurare SIGINT
    sa_int.sa_handler = handle_sigint;
    sa_int.sa_flags = 0;
    sigemptyset(&sa_int.sa_mask);
    sigaction(SIGINT, &sa_int, NULL);

    // 3. Crearea si scrierea in fisierul .monitor_pid
    // O_TRUNC asigura ca fisierul este suprascris daca exista deja
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

    printf("[MONITOR] Activ. PID-ul meu este: %d. Astept semnale...\n", my_pid);

    // 4. Bucla principala care tine programul in viata
    while (keep_running) {
        pause(); // Functia pause() adoarme procesul pana vine un semnal. (Nu consuma CPU)
    }

    // 5. Curatarea la final (se executa doar dupa ce keep_running devine 0 din cauza SIGINT)
    unlink(".monitor_pid");
    printf("[MONITOR] Fisierul PID a fost sters. La revedere!\n");

    return 0;
}