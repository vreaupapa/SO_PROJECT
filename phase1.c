#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>    // Pentru flag-uri (O_RDONLY, O_WRONLY, etc.)
#include <sys/stat.h> // Pentru moduri/permisiuni (S_IRUSR, etc.)
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define ARG 2

// A binary report file (reports.dat) storing fixed-size records, each containing at least:
// Report ID (integer)
// Inspector name (fixed-length string, provided as a --user argument)
// GPS coordinates (latitude and longitude as floating-point numbers)
// Issue category (fixed-length string, e.g. "road", "lighting", "flooding")
// Severity level (integer: 1 = minor, 2 = moderate, 3 = critical)
// Timestamp (time_t)
// Description text (fixed-length string)

typedef struct{
    int id;
    char inspector_name[50]; //provided as a --user argument
    float latitude;
    float longitude;
    char category[20];
    int severity;
    time_t timestamp;
    char description[256];
}Report;

char* get_timestamp() {
    time_t now = time(NULL);
    char* t = ctime(&now);
    t[strlen(t) - 1] = '\0';
    return t;
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
        perror("Only the manager can write in this file!\n");
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
    //Role check
    if(strcmp(role, "manager") != 0){
        printf("Error: Only managers can update thresholds.\n");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/district.cfg", district);

    //Permission check
    struct stat st;
    if(stat(path, &st) == -1){
        perror("Could not find config file");
        return;
    }

    if((st.st_mode & 0777) != 0640) {
        printf("CRITICAL ERROR: Permission mismatch! Expected 640, found %o. Refusing to write.\n", st.st_mode & 0777);
        return;
    }

    //Write the value
    //O_TRUNC empties the file
    int fd = open(path, O_WRONLY | O_TRUNC);
    if(fd != -1) {
        char val_str[10];
        int len = sprintf(val_str, "%d", new_value);
        write(fd, val_str, len);
        close(fd);
        printf("Threshold updated to %d\n", new_value);
    }

    log_operation(district, role, user, "Added new report");
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
        view_report(district, r.id);
    }
    close(fd);
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

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if(fd == -1) {
        perror("Error opening reports.dat");
        return;
    }
    //ensure permissions are exactly as requested using chmod
    chmod(path, 0664);
    //getting report data
    Report report;
    printf("Latitude: "); scanf("%f", &report.latitude);
    printf("Longitude: "); scanf("%f", &report.longitude);
    printf("Category: "); scanf("%s", report.category);
    printf("Severity level(1/2/3): "); scanf("%d", &report.severity);
    printf("Description: "); scanf("%s", report.description);
    strcpy(report.inspector_name, user);
    int fd2 = open(path, O_RDONLY);
    int cnt = 0;
    while(read(fd2, &report, sizeof(Report)) == sizeof(Report)){
        cnt++;
    }
    report.id = cnt;

    if(write(fd, &report, sizeof(Report)) == -1) {
        perror("Failed to write report");
    } else {
        printf("Report %d added successfully to %s\n", report.id, district_name);
    }

    close(fd);

    log_operation(district_name, role, user, "Added new report");
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
        }
    }
    if(!role || !user || !command) {
        printf("Wrong usage of commands!");
        return -1;
    }

    if(strcmp(command, "add") == 0){
        add(district_name, role, user);
    } else if(strcmp(command, "update_threshold") == 0){
        update_threshold(district_name, role, user, new_val);
    } else if(strcmp(command, "list") == 0){
        list_reports(district_name);
    } else if(strcmp(command, "view") == 0){
        view_report(district_name, report_id);
    }

    return 0;
}

//./p --role manager --user alice --update_threshold travis_scott 2
//./p --role manager --user alice --add travis_scott