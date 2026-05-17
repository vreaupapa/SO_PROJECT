#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void execute_start_monitor() {
    pid_t hub_mon = fork();

    if (hub_mon < 0) {
        perror("Fork failed for hub_mon");
        return;
    }

    if (hub_mon == 0) {
        // --- SUNTEM IN PROCESUL hub_mon (Copilul 1) ---
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("Pipe failed");
            exit(1);
        }

        pid_t monitor_pid = fork();

        if (monitor_pid == 0) {
            // --- SUNTEM IN PROCESUL monitor_reports (Copilul 2) ---
            close(pipefd[0]); // Inchidem capatul de citire al pipe-ului, nu avem nevoie
            
            // Redirectam STDOUT-ul sa curga in capatul de scriere al pipe-ului
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]); // Il putem inchide dupa dup2

            // Transformam acest proces in executabilul monitor_reports
            execl("./monitor_reports", "monitor_reports", NULL);
            perror("Eroare la pornirea monitor_reports (execl)");
            exit(1);
        } 
        else {
            // --- INAPOI IN hub_mon ---
            close(pipefd[1]); // Inchidem capatul de scriere, hub_mon doar citeste

            char buffer[256];
            int bytes_read;

            // Citim continuu din pipe tot ce printeaza monitorul
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0'; // Il facem string valid
                
                // Printam mesajul deasupra consolei
                printf("\n[HUB] Mesaj primit prin pipe: %s", buffer);
                printf("city_hub> ");
                fflush(stdout); // Fortam afisarea pe ecran imediat
            }

            // Daca functia read returneaza 0, inseamna ca pipe-ul s-a rupt
            // (procesul monitor a murit / s-a inchis)
            printf("\n[HUB ALERT] Procesul monitor s-a inchis oficial.\ncity_hub> ");
            fflush(stdout);
            close(pipefd[0]);
            exit(0); // hub_mon si-a terminat treaba
        }
    }
    // Parintele principal (city_hub) nu da wait(). Il lasa pe hub_mon in background
    printf("Comanda start_monitor trimisa in background.\n");
}

void execute_calculate_scores(char *args_str) {
    char *districts[20];
    int num_dist = 0;

    // Extragem districtele din comanda 
    char *token = strtok(args_str, " ");
    while (token != NULL && num_dist < 20) {
        districts[num_dist++] = token;
        token = strtok(NULL, " ");
    }

    if (num_dist == 0) {
        printf("Eroare: Trebuie sa oferi macar un district.\n");
        return;
    }

    int pipes[20][2];
    pid_t pids[20];

    // Parcurgem fiecare district si creem un proces + un pipe
    for (int i = 0; i < num_dist; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Eroare la crearea pipe-ului");
            continue;
        }

        pids[i] = fork();

        if (pids[i] == 0) {
            // --- IN PROCESUL COPIL (SCORER) ---
            close(pipes[i][0]); // Inchide citirea
            dup2(pipes[i][1], STDOUT_FILENO); // Redirectioneaza print-ul spre teava
            close(pipes[i][1]);

            execl("./scorer", "scorer", districts[i], NULL);
            perror("Eroare exec scorer");
            exit(1);
        }
        
        // --- IN PARINTE (city_hub) ---
        close(pipes[i][1]); // Parintele inchide scrierea imediat
    }

    // Colectam rezultatele din toate pipe-urile si le printam la un loc
    printf("\n--- RAPORT CUMULAT DE WORKLOAD ---\n\n");
    for (int i = 0; i < num_dist; i++) {
        char buffer[1024];
        int bytes_read;
        // Citim ce a scuipat fiecare program "scorer"
        while ((bytes_read = read(pipes[i][0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
        }
        close(pipes[i][0]);
        waitpid(pids[i], NULL, 0); // Asteptam ca scorer-ul curent sa isi termine executia complet
    }
}

int main() {
    char command_line[512];

    printf("=== Bine ai venit in City Hub Console ===\n");
    printf("Comenzi valabile:\n");
    printf("1. start_monitor\n");
    printf("2. calculate_scores dist1 dist2 ...\n");
    printf("3. exit\n\n");

    while (1) {
        printf("city_hub> ");
        if (fgets(command_line, sizeof(command_line), stdin) == NULL) {
            break;
        }

        // Eliminam \n de la finalul stringului introdus
        command_line[strcspn(command_line, "\n")] = 0;

        if (strlen(command_line) == 0) continue;

        if (strcmp(command_line, "exit") == 0) {
            printf("La revedere!\n");
            break;
        }

        // Extragem prima parte a comenzii
        char *cmd = strtok(command_line, " ");

        if (strcmp(cmd, "start_monitor") == 0) {
            execute_start_monitor();
        } 
        else if (strcmp(cmd, "calculate_scores") == 0) {
            // Pasam restul string-ului catre functie
            execute_calculate_scores(strtok(NULL, "")); 
        } 
        else {
            printf("Comanda necunoscuta.\n");
        }
    }

    return 0;
}