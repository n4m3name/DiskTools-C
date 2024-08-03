#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define MAX_DEPTH 64
#define MAX_CLUSTERS 65536
#define MAX_QUEUE_SIZE 1000

#pragma pack(push, 1)
struct BootSector {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_dir_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
};

struct DirEntry {
    char filename[8];
    char extension[3];
    uint8_t attributes;
    uint8_t reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t starting_cluster;
    uint32_t file_size;
};
#pragma pack(pop)

uint32_t get_fat_entry(uint8_t *fat, uint32_t cluster) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint16_t fat_entry = *(uint16_t*)&fat[fat_offset];
    if (cluster & 1) {
        return fat_entry >> 4;
    } else {
        return fat_entry & 0x0FFF;
    }
}

void print_datetime(uint16_t date, uint16_t time) {
    int year = ((date >> 9) & 0x7F) + 1980;
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hours = (time >> 11) & 0x1F;
    int minutes = (time >> 5) & 0x3F;
    int seconds = (time & 0x1F) * 2;
    
    printf("%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hours, minutes, seconds);
}

struct QueueItem {
    uint32_t cluster;
    char path[256];
    int depth;
};


void list_directory(FILE *file, uint32_t initial_cluster, struct BootSector *bs, uint8_t *fat, const char *initial_path) {
    struct QueueItem queue[MAX_QUEUE_SIZE];
    int front = 0, rear = 0;
    bool visited_clusters[MAX_CLUSTERS] = {false};

    // Enqueue the initial directory
    queue[rear].cluster = initial_cluster;
    strcpy(queue[rear].path, initial_path);
    queue[rear].depth = 0;
    rear++;

    while (front < rear) {
        uint32_t cluster = queue[front].cluster;
        char *path = queue[front].path;
        int depth = queue[front].depth;
        front++;

        if (depth >= MAX_DEPTH) {
            printf("Maximum directory depth reached. Skipping deeper directories.\n");
            continue;
        }

        printf("\n%s\n===================\n", path);

        do {
            if (cluster >= MAX_CLUSTERS || visited_clusters[cluster]) {
                break;
            }
            visited_clusters[cluster] = true;

            uint32_t sector, entries_to_read;
            if (cluster == 0) {
                sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16;
                entries_to_read = bs->root_dir_entries;
            } else {
                sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16 +
                         (bs->root_dir_entries * 32 + bs->bytes_per_sector - 1) / bs->bytes_per_sector +
                         (cluster - 2) * bs->sectors_per_cluster;
                entries_to_read = bs->bytes_per_sector / 32 * bs->sectors_per_cluster;
            }

            for (uint32_t i = 0; i < entries_to_read; i++) {
                struct DirEntry entry;
                fseek(file, sector * bs->bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET);
                fread(&entry, sizeof(struct DirEntry), 1, file);

                if (entry.filename[0] == 0) break; // End of directory
                if ((uint8_t)entry.filename[0] == 0xE5) continue; // Deleted entry
                if (entry.attributes == 0x0F) continue; // Long file name entry

                // Skip "." and ".." entries
                if (entry.filename[0] == '.' && (entry.filename[1] == ' ' || (entry.filename[1] == '.' && entry.filename[2] == ' '))) {
                    continue;
                }
                char filename[21];  // Increased to 21 to allow for null terminator
                snprintf(filename, sizeof(filename), "%.8s%.3s", entry.filename, entry.extension);
                for (int j = 11; j >= 0 && filename[j] == ' '; j--) {
                    filename[j] = '\0';
                }

                if (entry.starting_cluster == 0 || entry.starting_cluster == 1) continue; // Skip invalid entries

                if (entry.attributes & 0x10) {
                    printf("D %10s %-20s ", "", filename);  // Changed to 20 characters
                } else {
                    printf("F %10u %-20s ", entry.file_size, filename);  // Changed to 20 characters
                }
                print_datetime(entry.date, entry.time);
                printf("\n");

                if ((entry.attributes & 0x10) && entry.starting_cluster >= 2) {
                    char new_path[256];
                    snprintf(new_path, sizeof(new_path), "%s/%s", path, filename);
                    if (rear < MAX_QUEUE_SIZE) {
                        queue[rear].cluster = entry.starting_cluster;
                        strcpy(queue[rear].path, new_path);
                        queue[rear].depth = depth + 1;
                        rear++;
                    } else {
                        printf("Queue full. Skipping subdirectory: %s\n", new_path);
                    }
                }
            }

            if (cluster == 0) break;
            cluster = get_fat_entry(fat, cluster);
        } while (cluster < 0xFF8);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    struct BootSector bs;
    fread(&bs, sizeof(bs), 1, file);

    uint32_t fat_size = bs.fat_size_16;
    uint8_t *fat = malloc(fat_size * bs.bytes_per_sector);
    fseek(file, bs.reserved_sectors * bs.bytes_per_sector, SEEK_SET);
    fread(fat, fat_size * bs.bytes_per_sector, 1, file);

    // bool visited_clusters[MAX_CLUSTERS] = {false};
    // list_directory(file, 0, &bs, fat, "/", 0, visited_clusters);

    list_directory(file, 0, &bs, fat, "/");


    free(fat);
    fclose(file);
    return 0;
}
