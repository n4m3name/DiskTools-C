#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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

uint16_t read_fat_entry(FILE *disk, struct BootSector *bs, uint16_t cluster) {
    uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector + cluster * 3 / 2;
    uint16_t fat_entry;
    fseek(disk, fat_offset, SEEK_SET);
    fread(&fat_entry, 2, 1, disk);
    if (cluster & 1) {
        return fat_entry >> 4;
    } else {
        return fat_entry & 0x0FFF;
    }
}

void write_fat_entry(FILE *disk, struct BootSector *bs, uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector + cluster * 3 / 2;
    uint16_t fat_entry;
    fseek(disk, fat_offset, SEEK_SET);
    fread(&fat_entry, 2, 1, disk);
    if (cluster & 1) {
        fat_entry = (fat_entry & 0x000F) | (value << 4);
    } else {
        fat_entry = (fat_entry & 0xF000) | value;
    }
    fseek(disk, fat_offset, SEEK_SET);
    fwrite(&fat_entry, 2, 1, disk);
}

uint16_t find_free_cluster(FILE *disk, struct BootSector *bs) {
    for (uint16_t cluster = 2; cluster < bs->total_sectors_16 / bs->sectors_per_cluster; cluster++) {
        if (read_fat_entry(disk, bs, cluster) == 0) {
            return cluster;
        }
    }
    return 0xFFF; // No free cluster found
}

uint16_t find_directory(FILE *disk, struct BootSector *bs, const char *path) {

    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return 0; // Special case for root directory
    }

    
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    uint16_t current_cluster = 0; // Start from root directory

    while (token != NULL) {
        uint32_t dir_sector;
        uint32_t entries_to_read;
        
        if (current_cluster == 0) {
            dir_sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16;
            entries_to_read = bs->root_dir_entries;
        } else {
            dir_sector = bs->reserved_sectors + bs->num_fats * bs->fat_size_16 +
                         (bs->root_dir_entries * 32 + bs->bytes_per_sector - 1) / bs->bytes_per_sector +
                         (current_cluster - 2) * bs->sectors_per_cluster;
            entries_to_read = bs->bytes_per_sector / 32 * bs->sectors_per_cluster;
        }

        struct DirEntry entry;
        int found = 0;

        for (uint32_t i = 0; i < entries_to_read; i++) {
            fseek(disk, dir_sector * bs->bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET);
            fread(&entry, sizeof(entry), 1, disk);

            if (entry.filename[0] == 0) break; // End of directory
            if ((uint8_t)entry.filename[0] == 0xE5) continue; // Deleted entry

            char name[13];
            snprintf(name, sizeof(name), "%.8s%.3s", entry.filename, entry.extension);
            for (int j = 0; j < 12; j++) {
                if (name[j] == ' ') {
                    name[j] = '\0';
                    break;
                }
            }

            if (strcmp(name, token) == 0 && (entry.attributes & 0x10)) {
                current_cluster = entry.starting_cluster;
                found = 1;
                break;
            }
        }

        if (!found) {
            free(path_copy);
            return 0; // Directory not found
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_cluster;
}


int main(int argc, char *argv[]) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s <disk_image> [/path/to/]<filename>\n", argv[0]);
        return 1;
    }

    FILE *disk = fopen(argv[1], "r+b");
    if (!disk) {
        perror("Error opening disk image");
        return 1;
    }

    struct BootSector bs;
    fread(&bs, sizeof(bs), 1, disk);

    char *filepath = (argc == 4) ? argv[3] : argv[2];
    char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    char dirpath[256] = {0};
    if (filename != filepath) {
        strncpy(dirpath, filepath, filename - filepath - 1);
    }

    uint16_t dir_cluster = find_directory(disk, &bs, dirpath);
    if (dir_cluster == 0 && dirpath[0] != '\0') {
        printf("The directory not found.\n");
        fclose(disk);
        return 1;
    }

    FILE *input_file = fopen(filename, "rb");
    if (!input_file) {
        printf("File not found.\n");
        fclose(disk);
        return 1;
    }

    fseek(input_file, 0, SEEK_END);
    uint32_t file_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    uint32_t clusters_needed = (file_size + bs.bytes_per_sector * bs.sectors_per_cluster - 1) / (bs.bytes_per_sector * bs.sectors_per_cluster);
    uint32_t free_clusters = 0;
    for (uint16_t cluster = 2; cluster < bs.total_sectors_16 / bs.sectors_per_cluster; cluster++) {
        if (read_fat_entry(disk, &bs, cluster) == 0) {
            free_clusters++;
        }
    }

    if (free_clusters < clusters_needed) {
        printf("No enough free space in the disk image.\n");
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    uint32_t dir_sector;
    uint32_t entries_to_read;

    if (dir_cluster == 0) { // Root directory
        dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16;
        entries_to_read = bs.root_dir_entries;
    } else { // Subdirectory
        dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16 +
            (bs.root_dir_entries * 32 + bs.bytes_per_sector - 1) / bs.bytes_per_sector +
            (dir_cluster - 2) * bs.sectors_per_cluster;
        entries_to_read = bs.bytes_per_sector / 32 * bs.sectors_per_cluster;
    }

    // Find a free directory entry
    struct DirEntry entry;
    int free_entry_index = -1;
    for (uint32_t i = 0; i < entries_to_read; i++) {
        fseek(disk, dir_sector * bs.bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET);
        fread(&entry, sizeof(entry), 1, disk);
        if (entry.filename[0] == 0x00 || (unsigned char)entry.filename[0] == 0xE5) {
            free_entry_index = i;
            break;
        }
    }

    if (free_entry_index == -1) {
        printf("No free directory entries.\n");
        fclose(input_file);
        fclose(disk);
        return 1;
    }

    // Prepare the directory entry
    memset(&entry, 0, sizeof(entry));

    // Convert filename to uppercase and format it to 8.3
    char upper_filename[256];
    for (int i = 0; filename[i]; i++) {
        upper_filename[i] = toupper((unsigned char)filename[i]);
    }
    upper_filename[255] = '\0';

    char *dot = strrchr(upper_filename, '.');
    char base_name[9] = "        ";  // 8 spaces
    char extension[4] = "   ";       // 3 spaces

    if (dot) {
        strncpy(base_name, upper_filename, dot - upper_filename > 8 ? 8 : dot - upper_filename);
        strncpy(extension, dot + 1, 3);
    } else {
        strncpy(base_name, upper_filename, 8);
    }

    memcpy(entry.filename, base_name, 8);
    memcpy(entry.extension, extension, 3);

    entry.attributes = 0x00; // Regular file
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    entry.time = (tm_now->tm_hour << 11) | (tm_now->tm_min << 5) | (tm_now->tm_sec / 2);
    entry.date = ((tm_now->tm_year - 80) << 9) | ((tm_now->tm_mon + 1) << 5) | tm_now->tm_mday;
    entry.file_size = file_size;

    // Find the first free cluster and write the file
    uint16_t first_cluster = find_free_cluster(disk, &bs);
    entry.starting_cluster = first_cluster;
    uint16_t current_cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < file_size) {
        uint32_t cluster_start;
        if (current_cluster < 2) {
            printf("Invalid cluster number.\n");
            fclose(input_file);
            fclose(disk);
            return 1;
        }
        cluster_start = bs.reserved_sectors * bs.bytes_per_sector +
            bs.num_fats * bs.fat_size_16 * bs.bytes_per_sector +
            bs.root_dir_entries * 32 +
            (current_cluster - 2) * bs.sectors_per_cluster * bs.bytes_per_sector;

        uint32_t to_write = bs.sectors_per_cluster * bs.bytes_per_sector;
        if (to_write > file_size - bytes_written) {
            to_write = file_size - bytes_written;
        }

        char buffer[4096];
        fread(buffer, 1, to_write, input_file);
        fseek(disk, cluster_start, SEEK_SET);
        fwrite(buffer, 1, to_write, disk);
        bytes_written += to_write;

        if (bytes_written < file_size) {
            uint16_t next_cluster = find_free_cluster(disk, &bs);
            write_fat_entry(disk, &bs, current_cluster, next_cluster);
            current_cluster = next_cluster;
        } else {
            write_fat_entry(disk, &bs, current_cluster, 0xFFF); // End of file
        }
    }

    // Write the directory entry
    fseek(disk, dir_sector * bs.bytes_per_sector + free_entry_index * sizeof(struct DirEntry), SEEK_SET);
    fwrite(&entry, sizeof(entry), 1, disk);

    fclose(input_file);
    fclose(disk);
    printf("File copied successfully.\n");
    return 0;
}




// int main(int argc, char *argv[]) {
//     if (argc != 3 && argc != 4) {
//         fprintf(stderr, "Usage: %s <disk_image> [/path/to/]<filename>\n", argv[0]);
//         return 1;
//     }

//     FILE *disk = fopen(argv[1], "r+b");
//     if (!disk) {
//         perror("Error opening disk image");
//         return 1;
//     }

//     struct BootSector bs;
//     fread(&bs, sizeof(bs), 1, disk);

//     char *filepath = (argc == 4) ? argv[3] : argv[2];
//     char *filename = strrchr(filepath, '/');
//     filename = filename ? filename + 1 : filepath;

//     char dirpath[256] = {0};
//     if (filename != filepath) {
//         strncpy(dirpath, filepath, filename - filepath - 1);
//     }

//     uint16_t dir_cluster = find_directory(disk, &bs, dirpath);
//     if (dir_cluster == 0 && dirpath[0] != '\0') {
//         printf("The directory not found.\n");
//         fclose(disk);
//         return 1;
//     }

//     FILE *input_file = fopen(filename, "rb");
//     if (!input_file) {
//         printf("File not found.\n");
//         fclose(disk);
//         return 1;
//     }

//     fseek(input_file, 0, SEEK_END);
//     uint32_t file_size = ftell(input_file);
//     fseek(input_file, 0, SEEK_SET);

//     uint32_t clusters_needed = (file_size + bs.bytes_per_sector * bs.sectors_per_cluster - 1) / (bs.bytes_per_sector * bs.sectors_per_cluster);
//     uint32_t free_clusters = 0;
//     for (uint16_t cluster = 2; cluster < bs.total_sectors_16 / bs.sectors_per_cluster; cluster++) {
//         if (read_fat_entry(disk, &bs, cluster) == 0) {
//             free_clusters++;
//         }
//     }

//     if (free_clusters < clusters_needed) {
//         printf("No enough free space in the disk image.\n");
//         fclose(input_file);
//         fclose(disk);
//         return 1;
//     }

//     uint32_t dir_sector;
//     uint32_t entries_to_read;

//     if (dir_cluster == 0) { // Root directory
//         dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16;
//         entries_to_read = bs.root_dir_entries;
//     } else { // Subdirectory
//         dir_sector = bs.reserved_sectors + bs.num_fats * bs.fat_size_16 +
//             (bs.root_dir_entries * 32 + bs.bytes_per_sector - 1) / bs.bytes_per_sector +
//             (dir_cluster - 2) * bs.sectors_per_cluster;
//         entries_to_read = bs.bytes_per_sector / 32 * bs.sectors_per_cluster;
//     }

//     // Find a free directory entry
//     struct DirEntry entry;
//     int free_entry_index = -1;
//     for (uint32_t i = 0; i < entries_to_read; i++) {
//         fseek(disk, dir_sector * bs.bytes_per_sector + i * sizeof(struct DirEntry), SEEK_SET);
//         fread(&entry, sizeof(entry), 1, disk);
//         if (entry.filename[0] == 0x00 || (unsigned char)entry.filename[0] == 0xE5) {
//             free_entry_index = i;
//             break;
//         }
//     }

//     if (free_entry_index == -1) {
//         printf("No free directory entries.\n");
//         fclose(input_file);
//         fclose(disk);
//         return 1;
//     }

//     // Prepare the directory entry
//     memset(&entry, 0, sizeof(entry));
//     strncpy(entry.filename, filename, 8);
//     char *ext = strrchr(filename, '.');
//     if (ext) {
//         strncpy(entry.extension, ext + 1, 3);
//     }
//     entry.attributes = 0x00; // Regular file
//     time_t now = time(NULL);
//     struct tm *tm_now = localtime(&now);
//     entry.time = (tm_now->tm_hour << 11) | (tm_now->tm_min << 5) | (tm_now->tm_sec / 2);
//     entry.date = ((tm_now->tm_year - 80) << 9) | ((tm_now->tm_mon + 1) << 5) | tm_now->tm_mday;
//     entry.file_size = file_size;

//     // Find the first free cluster and write the file
//     uint16_t first_cluster = find_free_cluster(disk, &bs);
//     entry.starting_cluster = first_cluster;

//     // Find the first free cluster and write the file
//     // uint16_t first_cluster = find_free_cluster(disk, &bs);
//     entry.starting_cluster = first_cluster;
//     uint16_t current_cluster = first_cluster;
//     uint32_t bytes_written = 0;

//     while (bytes_written < file_size) {
//         uint32_t cluster_start;
//         if (current_cluster < 2) {
//             // This shouldn't happen, but just in case
//             printf("Invalid cluster number.\n");
//             fclose(input_file);
//             fclose(disk);
//             return 1;
//         }
//         cluster_start = bs.reserved_sectors * bs.bytes_per_sector +
//             bs.num_fats * bs.fat_size_16 * bs.bytes_per_sector +
//             bs.root_dir_entries * 32 +
//             (current_cluster - 2) * bs.sectors_per_cluster * bs.bytes_per_sector;

//         uint32_t to_write = bs.sectors_per_cluster * bs.bytes_per_sector;
//         if (to_write > file_size - bytes_written) {
//             to_write = file_size - bytes_written;
//         }

//         char buffer[4096];
//         fread(buffer, 1, to_write, input_file);
//         fseek(disk, cluster_start, SEEK_SET);
//         fwrite(buffer, 1, to_write, disk);
//         bytes_written += to_write;

//         if (bytes_written < file_size) {
//             uint16_t next_cluster = find_free_cluster(disk, &bs);
//             write_fat_entry(disk, &bs, current_cluster, next_cluster);
//             current_cluster = next_cluster;
//         } else {
//             write_fat_entry(disk, &bs, current_cluster, 0xFFF); // End of file
//         }
//     }

//     // Write the directory entry
//     fseek(disk, dir_sector * bs.bytes_per_sector + free_entry_index * sizeof(struct DirEntry), SEEK_SET);
//     fwrite(&entry, sizeof(entry), 1, disk);

//     fclose(input_file);
//     fclose(disk);
//     printf("File copied successfully.\n");
//     return 0;
// }



