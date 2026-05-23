#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TIME 1000 // support max 1000 time slots
#define MAX_CPUS 16   // support max 16 CPUs

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file_log.txt>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        printf("Cannot open file %s\n", argv[1]);
        return 1;
    }

    int schedule[MAX_CPUS][MAX_TIME];
    for(int c = 0; c < MAX_CPUS; c++) {
        for(int i = 0; i < MAX_TIME; i++) schedule[c][i] = -1;
    }

    char line[256];
    int current_time = -1;
    int active_pid[MAX_CPUS];
    for(int c = 0; c < MAX_CPUS; c++) active_pid[c] = -1;

    int max_time = 0;
    int max_cpu = 0;

    // READ AND ANALYZE FILE LOG
    while (fgets(line, sizeof(line), f)) {
        int t;
        // Parse time slot line
        if (sscanf(line, "Time slot %d", &t) == 1) {
            if (current_time != -1 && current_time < t) {
                for (int i = current_time; i < t; i++) {
                    for (int c = 0; c <= max_cpu; c++) {
                        schedule[c][i] = active_pid[c];
                    }
                }
            }
            current_time = t;
            if (t > max_time) max_time = t;
        }

        int cpu_id, pid;
        // Parse dispatch line
        if (strstr(line, "Dispatched process")) {
            char *p1 = strstr(line, "CPU ");
            char *p2 = strstr(line, "process ");
            if (p1 && p2) {
                sscanf(p1, "CPU %d", &cpu_id);
                sscanf(p2, "process %d", &pid);
                if (cpu_id >= 0 && cpu_id < MAX_CPUS) {
                    active_pid[cpu_id] = pid;
                    if (cpu_id > max_cpu) max_cpu = cpu_id;
                }
            }
        }

        // Parse put back or finished or stopped line
        if (strstr(line, "Put process") || strstr(line, "has finished") || strstr(line, "stopped")) {
            char *p1 = strstr(line, "CPU ");
            if (p1) {
                sscanf(p1, "CPU %d", &cpu_id);
                if (cpu_id >= 0 && cpu_id < MAX_CPUS) {
                    active_pid[cpu_id] = -1; // CPU is idle
                    if (cpu_id > max_cpu) max_cpu = cpu_id;
                }
            }
        }
    }
    fclose(f);

    // Print GANTT chart
    printf("\n=== GANTT CHART (%s) ===\n\n", argv[1]);

    int chunk_size = 20;
    for (int start = 0; start <= max_time; start += chunk_size) {
        int end = start + chunk_size - 1;
        if (end > max_time) end = max_time;

        // Print time range
        printf("Time : ");
        for (int i = start; i <= end; i++) {
            printf("%02d ", i);
        }
        printf("\n");

        // Print CPU schedule
        for (int c = 0; c <= max_cpu; c++) {
            printf("CPU %d: ", c);
            for (int i = start; i <= end; i++) {
                if (schedule[c][i] == -1) {
                    printf("-- ");
                } else {
                    int color = 31 + (schedule[c][i] % 6);
                    printf("\033[1;%dmP%d\033[0m ", color, schedule[c][i]);
                }
            }
            printf("\n");
        }
        printf("\n");
    }

    return 0;
}
