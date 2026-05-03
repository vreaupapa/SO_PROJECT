#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>    // Pentru flag-uri (O_RDONLY, O_WRONLY, etc.)
#include <sys/stat.h> // Pentru moduri/permisiuni (S_IRUSR, etc.)
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define ARG 2

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

char* get_timestamp() {
    time_t now = time(NULL);
    char* t = ctime(&now);
    t[strlen(t) - 1] = '\0';
    return t;
}

// Functia 1: Desparte sirul "field:op:value" in 3 parti
int parse_condition(const char *input, char *field, char *op, char *value) {
    // sscanf cauta formatul: orice pana la :, apoi orice pana la :, apoi restul
    int scanned = sscanf(input, "%[^:]:%[^:]:%s", field, op, value);
    return (scanned == 3);
}

// Functia 2: Verifica daca un raport (r) respecta o singura conditie
int match_condition(Report *r, const char *field, const char *op, const char *value) {
    if (strcmp(field, "severity") == 0) {
        int val = atoi(value);
        if (strcmp(op, "==") == 0) return r->severity == val;
        if (strcmp(op, "!=") == 0) return r->severity != val;
        if (strcmp(op, ">") == 0)  return r->severity > val;
        if (strcmp(op, ">=") == 0) return r->severity >= val;
        if (strcmp(op, "<") == 0)  return r->severity < val;
        if (strcmp(op, "<=") == 0) return r->severity <= val;
    } 
    else if (strcmp(field, "category") == 0) {
        if (strcmp(op, "==") == 0) return strcmp(r->category, value) == 0;
        if (strcmp(op, "!=") == 0) return strcmp(r->category, value) != 0;
    }
    else if (strcmp(field, "inspector") == 0) {
        if (strcmp(op, "==") == 0) return strcmp(r->inspector_name, value) == 0;
        if (strcmp(op, "!=") == 0) return strcmp(r->inspector_name, value) != 0;
    }
    else if (strcmp(field, "timestamp") == 0) {
        time_t val = (time_t)atoll(value);
        if (strcmp(op, "==") == 0) return r->timestamp == val;
        if (strcmp(op, ">") == 0)  return r->timestamp > val;
        if (strcmp(op, "<") == 0)  return r->timestamp < val;
    }
    return 0;
}

void filter_reports(const char *district, int num_conditions, char **conditions) {
    char path[256];
    snprintf(path, sizeof(path), "%s/reports.dat", district);

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening reports for filtering");
        return;
    }

    Report r;
    int found_any = 0;

    // Citim fiecare raport din fisierul binar
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        int matches_all = 1;

        // Verificam raportul curent impotriva TUTUROR conditiilor primite
        for (int i = 0; i < num_conditions; i++) {
            char f[50], o[10], v[100];
            if (parse_condition(conditions[i], f, o, v)) {
                if (!match_condition(&r, f, o, v)) {
                    matches_all = 0; // Daca o singura conditie pica, raportul e respins
                    break;
                }
            }
        }

        if (matches_all) {
            printf("[MATCH] ID: %d | Cat: %s | Sev: %d | Insp: %s | Desc: %s\n", 
                   r.id, r.category, r.severity, r.inspector_name, r.description);
            found_any = 1;
        }
    }

    if (!found_any) {
        printf("No reports matched the given conditions.\n");
    }

    close(fd);
}

void create_district_dir(const char* district_name){
    if(mkdir(district_name, 0750) == -1){
        if(errno == EEXIST){
            printf("The directory already exists!\n");
        } else {
            perror("Error for creating the file!\n");
        }
    } else {
        chmod(district_name, 0750);
        printf("The directory was successfully created!");
    }
}

void create_config_file(const char* district_name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/district.cfg", district_name);

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0640);
    if(fd != -1){
        write(fd, "1", 1);
        close(fd);
        printf("Created default config for %s\n", district_name);
    }
}

void log_operation(const char* district, const char* role, const char* user, const char* msg){
    if(strcmp(role, "manager") != 0){
        printf("Inspector action recorded but not logged to file (Manager required).\n");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/logged_district", district);

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd == -1) {
        perror("ERROR opening log file");
        return;
    }

    char entry[512];
    int len = snprintf(entry, sizeof(entry), "[%s] %s (%s): %s\n", get_timestamp(), user, role, msg);

    write(fd, entry, len);
    close(fd);
}

void update_threshold(const char* district, const char* role, const char* user, int new_value){
    if(strcmp(role, "manager") != 0){
        printf("Error: Only managers can update thresholds.\n");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/district.cfg", district);

    struct stat st;
    if(stat(path, &st) == -1){
        perror("Could not find config file");
        return;
    }

    if((st.st_mode & 0777) != 0640) {
        printf("CRITICAL ERROR: Permission mismatch! Expected 640, found %o. Refusing to write.\n", st.st_mode & 0777);
        return;
    }

    int fd = open(path, O_WRONLY | O_TRUNC);
    if(fd != -1) {
        char val_str[10];
        int len = sprintf(val_str, "%d", new_value);
        write(fd, val_str, len);
        close(fd);
        printf("Threshold updated to %d\n", new_value);
    }

    log_operation(district, role, user, "Updated threshold");
}

int has_access(const char *path, const char *role, mode_t bit_manager, mode_t bit_inspector){
    struct stat st;
    if(stat(path, &st) == -1) {
        return 1;
    }

    if(strcmp(role, "manager") == 0) {
        return (st.st_mode & bit_manager);
    } else if (strcmp(role, "inspector") == 0) {
        return (st.st_mode & bit_inspector);
    }
    return 0;
}

void check_symlink_status(const char* district_name) {
    char link_name[256];
    snprintf(link_name, sizeof(link_name), "active_reports-%s", district_name);

    struct stat st_link, st_target;

    if (lstat(link_name, &st_link) == -1) {
        return;
    }

    if (S_ISLNK(st_link.st_mode)) {
        if (stat(link_name, &st_target) == -1) {
            printf("WARNING: Symlink %s is dangling! (Target missing)\n", link_name);
        } else {
            printf("Symlink %s is healthy.\n", link_name);
        }
    }
}

void manage_symlink(const char* district_name) {
    char target[256];
    char link_name[256];

    snprintf(target, sizeof(target), "%s/reports.dat", district_name);
    snprintf(link_name, sizeof(link_name), "active_reports-%s", district_name);

    unlink(link_name);

    if (symlink(target, link_name) == -1) {
        perror("Error creating symlink");
    } else {
        printf("Symlink created: %s -> %s\n", link_name, target);
    }
}

void get_permissions_string(mode_t mode, char *str){
    strcpy(str, "---------");
    if(mode & S_IRUSR) str[0] = 'r';
    if(mode & S_IWUSR) str[1] = 'w';
    if(mode & S_IXUSR) str[2] = 'x';
    if(mode & S_IRGRP) str[3] = 'r';
    if(mode & S_IWGRP) str[4] = 'w';
    if(mode & S_IXGRP) str[5] = 'x';
    if(mode & S_IROTH) str[6] = 'r';
    if(mode & S_IWOTH) str[7] = 'w';
    if(mode & S_IXOTH) str[8] = 'x';
}

void view_report(const char* district, int report_id){
    char path[256];
    sprintf(path, "%s/reports.dat", district);

    struct stat st;
    if(stat(path, &st)==-1){
        printf("District %s has no reports available\n", district);
        return;
    }
    int fd = open(path, O_RDONLY);
    Report r;
    while(read(fd, &r, sizeof(Report)) == sizeof(Report)){
        if(r.id == report_id){
            printf("ID: %d | Cat: %s | Sev: %d | Insp: %s\n", r.id, r.category, r.severity, r.inspector_name);
        }
    }
    close(fd);
}

void list_reports(const char *district){
    check_symlink_status(district);
    char path[256];
    sprintf(path, "%s/reports.dat", district);

    struct stat st;
    if(stat(path, &st) < 0){
        printf("District %s has no reports available.\n", district);
        return;
    }

    char perms[11];
    get_permissions_string(st.st_mode, perms);
    printf("File:%s , Perms:%s , Size: %ld bytes\n", path, perms, st.st_size);

    int fd = open(path, O_RDONLY);
    Report r;
    while(read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        printf("Report Found: ID %d\n", r.id);
    }
    close(fd);
}



void remove_report(const char* district, const char* role, const char* user, int report_id) {
    if (strcmp(role, "manager") != 0) {
        printf("Error: Only managers can remove reports.\n");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/reports.dat", district);

    int fd = open(path, O_RDWR);
    if (fd == -1) {
        perror("Error opening reports.dat for removal");
        return;
    }

    Report r;
    int found = 0;
    long write_pos = 0;
    long read_pos = 0;

    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        if (r.id == report_id) {
            found = 1;
            write_pos = lseek(fd, 0, SEEK_CUR) - sizeof(Report);
            break;
        }
    }

    if (!found) {
        printf("Report ID %d not found in district %s.\n", report_id, district);
        close(fd);
        return;
    }

    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        read_pos = lseek(fd, 0, SEEK_CUR); 
        lseek(fd, write_pos, SEEK_SET);    
        write(fd, &r, sizeof(Report));     
        write_pos = lseek(fd, 0, SEEK_CUR); 
        lseek(fd, read_pos, SEEK_SET);     
    }

    struct stat st;
    fstat(fd, &st);
    if (ftruncate(fd, st.st_size - sizeof(Report)) == -1) {
        perror("Error truncating file");
    } else {
        printf("Report %d removed successfully.\n", report_id);
    }

    close(fd);
    log_operation(district, role, user, "Removed a report");
}

void remove_district(const char* district_name, const char* role, const char* user){
    //verifying the role, it need to be manager
    if(strcmp("manager", role)!=0){
        printf("Error: Only managers can remove entire districts.\n");
        return;
    }

    if(district_name == NULL || strlen(district_name) == 0 || strcmp(district_name, ".") == 0 || strcmp(district_name, "..") == 0){
        printf("Error: Invalid district_name");
        return;
    }

    char link_name[256];
    snprintf(link_name, sizeof(link_name), "active_reports-%s", district_name);
    if(unlink(link_name) == 0){
        printf("Symlink %s removed.\n", link_name);
    } else {
        perror("Warning: Could not remove Symlink(might not exist)");
    }

    pid_t pid = fork();

    if(pid < 0){
        perror("Fork failed");
        return;
    }

    if(pid == 0){
        //we are in the child process
        execlp("rm", "rm", "-rf", district_name, NULL);

        perror("Exec failed");
        exit(EXIT_FAILURE);
    } else {
        //we are in the parent process
        int status;
        wait(&status);

        if(WIFEXITED(status) && WEXITSTATUS(status) == 0){
            printf("District %s and all its contents have been successfully deleted.\n", district_name);
        } else {
            printf("Error: 'rm' command failed to delete the district");
        }
    }
}

void add(const char* district_name, const char* role, const char* user){
    create_district_dir(district_name);
    create_config_file(district_name);

    char path[256];
    snprintf(path, sizeof(path), "%s/reports.dat", district_name);

    if(!has_access(path, role, S_IWUSR, S_IWGRP)) {
        fprintf(stderr, "Access denied for role: %s\n", role);
        return;
    }

    int fd = open(path, O_RDWR | O_CREAT, 0664);
    if(fd == -1) {
        perror("Error opening reports.dat");
        return;
    }
    chmod(path, 0664);

    Report report;
    struct stat st;
    fstat(fd, &st);
    
    if (st.st_size == 0) {
        report.id = 1; 
    } else {
        lseek(fd, -sizeof(Report), SEEK_END);
        Report last_report;
        if (read(fd, &last_report, sizeof(Report)) == sizeof(Report)) {
            report.id = last_report.id + 1;
        } else {
            report.id = 1;
        }
    }
    lseek(fd, 0, SEEK_END);

    printf("Adding report with ID: %d\n", report.id);
    printf("Latitude: "); scanf("%f", &report.latitude);
    printf("Longitude: "); scanf("%f", &report.longitude);
    printf("Category: "); scanf("%s", report.category);
    printf("Severity level(1/2/3): "); scanf("%d", &report.severity);
    
    getchar(); 
    printf("Description: ");
    fgets(report.description, sizeof(report.description), stdin);
    report.description[strcspn(report.description, "\n")] = 0; 

    strcpy(report.inspector_name, user);
    report.timestamp = time(NULL);

    if(write(fd, &report, sizeof(Report)) == -1) {
        perror("Failed to write report");
    } else {
        printf("Report %d added successfully to %s\n", report.id, district_name);
    }

    manage_symlink(district_name);
    close(fd);

    //pentru part 2

    int monitor_fd = open(".monitor_pid", O_RDONLY);
    int monitor_notified = 0; // Un steag pentru a sti ce scriem in log

    if (monitor_fd != -1) {
        char pid_buffer[32] = {0};
        int bytes_read = read(monitor_fd, pid_buffer, sizeof(pid_buffer) - 1);
        
        if (bytes_read > 0) {
            pid_t monitor_pid = atoi(pid_buffer);
            
            // Trimitem semnalul SIGUSR1 cu functia kill()
            // kill returneaza 0 pe succes si -1 la eroare (ex: procesul nu mai exista)
            if (kill(monitor_pid, SIGUSR1) == 0) {
                monitor_notified = 1;
            }
        }
        close(monitor_fd);
    }

    // --- LOGAREA CONFORM CERINTEI ---
    char log_msg[256];
    if (monitor_notified) {
        snprintf(log_msg, sizeof(log_msg), "Added new report (Monitor successfully notified)");
    } else {
        snprintf(log_msg, sizeof(log_msg), "Added new report (Failed to notify monitor: missing PID file or process is dead)");
    }
    
    log_operation(district_name, role, user, log_msg);
}

int main(int argc, char** argv){
    char *role = NULL;
    char *user = NULL;
    char *district_name = NULL;
    char *command = NULL;
    int new_val;
    int report_id;

    for(int i=1; i < argc; i++){
        if(strcmp(argv[i], "--role") == 0) role = argv[++i];
        else if(strcmp(argv[i], "--user") == 0) user = argv[++i];
        else if(strcmp(argv[i], "--add") == 0) {
            district_name = argv[++i];
            command = "add";
        } else if(strcmp(argv[i], "--update_threshold") == 0) {
            command = "update_threshold";
            district_name = argv[++i];
            new_val = atoi(argv[++i]);
        } else if(strcmp(argv[i], "--list") == 0){
            command = "list";
            district_name = argv[++i];
        } else if(strcmp(argv[i], "--view") == 0){
            command = "view";
            district_name = argv[++i];
            report_id = atoi(argv[++i]);
        } else if(strcmp(argv[i], "--remove_report") == 0){
            command = "remove_report";
            district_name = argv[++i];
            report_id = atoi(argv[++i]);
        } else if(strcmp(argv[i], "--filter") == 0){
            command = "filter";
            district_name = argv[++i];
            int num_cond = 0;
            char *conds[10]; 
            while (i + 1 < argc && argv[i+1][0] != '-') { 
                conds[num_cond++] = argv[++i];
            }
            filter_reports(district_name, num_cond, conds);
            return 0; 
        } else if(strcmp(argv[i], "--remove_district") == 0){
            command = "remove_district";
            district_name = argv[++i];
        }
    }
    
    if(!role || !user || !command) {
        printf("Wrong usage of commands!");
        return -1;
    }

    if(strcmp(command, "add") == 0) add(district_name, role, user);
    else if(strcmp(command, "update_threshold") == 0) update_threshold(district_name, role, user, new_val);
    else if(strcmp(command, "list") == 0) list_reports(district_name);
    else if(strcmp(command, "view") == 0) view_report(district_name, report_id);
    else if(strcmp(command, "remove_report") == 0) remove_report(district_name, role, user, report_id);
    else if(strcmp(command, "remove_district") == 0) remove_district(district_name, role, user);

    return 0;
}