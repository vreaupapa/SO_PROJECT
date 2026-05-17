#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

// Structura exacta a raportului tau din city_manager
typedef struct {
    int id;
    char inspector_name[50]; 
    float latitude;
    float longitude;
    char category[20];
    int severity;
    time_t timestamp;
    char description[256];
} Report;

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Eroare: Lipseste numele districtului.\n");
        return 1;
    }

    const char *district = argv[1];
    char path[256];
    snprintf(path, sizeof(path), "%s/reports.dat", district);

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        printf("--- [District: %s] Nu exista rapoarte sau districtul este invalid. ---\n", district);
        return 0;
    }

    // Array simplu pentru a tine scorurile. 
    // Presupunem un maxim de 100 de inspectori diferiti per district.
    struct {
        char name[50];
        int score;
    } inspectors[100];
    
    int num_inspectors = 0;
    Report r;

    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        int found = 0;
        for (int i = 0; i < num_inspectors; i++) {
            if (strcmp(inspectors[i].name, r.inspector_name) == 0) {
                inspectors[i].score += r.severity;
                found = 1;
                break;
            }
        }
        if (!found && num_inspectors < 100) {
            strcpy(inspectors[num_inspectors].name, r.inspector_name);
            inspectors[num_inspectors].score = r.severity;
            num_inspectors++;
        }
    }
    close(fd);

    // Formatam output-ul. El va fi capturat automat de pipe in city_hub!
    printf("=== Scorul muncii pentru districtul: %s ===\n", district);
    for (int i = 0; i < num_inspectors; i++) {
        printf(" Inspector: %-15s | Punctaj Severitate Total: %d\n", inspectors[i].name, inspectors[i].score);
    }
    printf("\n");

    return 0;
}