// #include <stdio.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include <string.h>

// #pragma pack(push, 1)
// struct BootSector {
//     uint8_t jmp[3];
//     char oem[8];
//     uint16_t bytes_per_sector;
//     uint8_t sectors_per_cluster;
//     uint16_t reserved_sectors;
//     uint8_t num_fats;
//     uint16_t root_dir_entries;
//     uint16_t total_sectors_16;
//     uint8_t media_type;
//     uint16_t fat_size_16;
//     uint16_t sectors_per_track;
//     uint16_t num_heads;
//     uint32_t hidden_sectors;
//     uint32_t total_sectors_32;
// };

// struct DirEntry {
//     char filename[8];
//     char extension[3];
//     uint8_t attributes;
//     uint8_t reserved[10];
//     uint16_t time;
//     uint16_t date;
//     uint16_t starting_cluster;
//     uint32_t file_size;
// };
// #pragma pack(pop)

// uint16_t read_fat_entry(FILE *disk, struct BootSector *bs, uint16_t cluster) {
//     uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector + cluster * 3 / 2;
//     uint16_t fat_entry;
//     fseek(disk, fat_offset, SEEK_SET);
//     fread(&fat_entry, 2, 1, disk);
//     if (cluster & 1) {
//         return fat_entry >> 4;
//     } else {
//         return fat_entry & 0x0FFF;
//     }
// }

// int main(int argc, char *argv[]) {
//     if (argc != 3) {
//         fprintf(stderr, "Usage: %s <disk_image> <filename>\n", argv[0]);
//         return 1;
//     }

//     FILE *disk = fopen(argv[1], "rb");
//     if (!disk) {
//         perror("Error opening disk image");
//         return 1;
//     }

//     struct BootSector bs;
//     fread(&bs, sizeof(bs), 1, disk);

//     uint32_t root_dir_start = (bs.reserved_sectors + bs.num_fats * bs.fat_size_16) * bs.bytes_per_sector;
//     uint32_t data_start = root_dir_start + bs.root_dir_entries * 32;

//     fseek(disk, root_dir_start, SEEK_SET);

//     struct DirEntry entry;
//     int found = 0;
//     for (int i = 0; i < bs.root_dir_entries; i++) {
//         fread(&entry, sizeof(entry), 1, disk);
//         char filename[13];
//         snprintf(filename, sizeof(filename), "%.8s%.3s", entry.filename, entry.extension);
        
//         // Remove trailing spaces
//         for (int j = 11; j >= 0 && filename[j] == ' '; j--) {
//             filename[j] = '\0';
//         }

//         if (strcmp(filename, argv[2]) == 0) {
//             found = 1;
//             break;
//         }
//     }

//     if (!found) {
//         printf("File not found.\n");
//         fclose(disk);
//         return 1;
//     }

//     FILE *output = fopen(argv[2], "wb");
//     if (!output) {
//         perror("Error creating output file");
//         fclose(disk);
//         return 1;
//     }

//     uint16_t current_cluster = entry.starting_cluster;
//     uint32_t bytes_remaining = entry.file_size;

//     while (current_cluster < 0xFF8 && bytes_remaining > 0) {
//         uint32_t cluster_start = data_start + (current_cluster - 2) * bs.sectors_per_cluster * bs.bytes_per_sector;
//         fseek(disk, cluster_start, SEEK_SET);

//         uint32_t to_read = bs.sectors_per_cluster * bs.bytes_per_sector;
//         if (to_read > bytes_remaining) {
//             to_read = bytes_remaining;
//         }

//         char buffer[to_read];
//         fread(buffer, 1, to_read, disk);
//         fwrite(buffer, 1, to_read, output);

//         bytes_remaining -= to_read;
//         current_cluster = read_fat_entry(disk, &bs, current_cluster);
//     }

//     fclose(output);
//     fclose(disk);
//     printf("File copied successfully.\n");
//     return 0;
// }


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <disk_image> <filename>\n", argv[0]);
        return 1;
    }

    FILE *disk = fopen(argv[1], "rb");
    if (!disk) {
        perror("Error opening disk image");
        return 1;
    }

    struct BootSector bs;
    fread(&bs, sizeof(bs), 1, disk);

    uint32_t root_dir_start = (bs.reserved_sectors + bs.num_fats * bs.fat_size_16) * bs.bytes_per_sector;
    uint32_t data_start = root_dir_start + bs.root_dir_entries * 32;

    fseek(disk, root_dir_start, SEEK_SET);

    struct DirEntry entry;
    int found = 0;
    for (int i = 0; i < bs.root_dir_entries; i++) {
        fread(&entry, sizeof(entry), 1, disk);
        char filename[13];
        int j;
        
        // Copy filename, removing spaces
        for (j = 0; j < 8 && entry.filename[j] != ' '; j++) {
            filename[j] = tolower(entry.filename[j]);
        }
        
        // Add dot if extension exists
        if (entry.extension[0] != ' ') {
            filename[j++] = '.';
            for (int k = 0; k < 3 && entry.extension[k] != ' '; k++) {
                filename[j++] = tolower(entry.extension[k]);
            }
        }
        filename[j] = '\0';

        // Convert input filename to lowercase for comparison
        char input_filename[13];
        strncpy(input_filename, argv[2], sizeof(input_filename) - 1);
        input_filename[sizeof(input_filename) - 1] = '\0';
        for (int k = 0; input_filename[k]; k++) {
            input_filename[k] = tolower(input_filename[k]);
        }

        if (strcmp(filename, input_filename) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("File not found.\n");
        fclose(disk);
        return 1;
    }

    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror("Error creating output file");
        fclose(disk);
        return 1;
    }

    uint16_t current_cluster = entry.starting_cluster;
    uint32_t bytes_remaining = entry.file_size;

    while (current_cluster < 0xFF8 && bytes_remaining > 0) {
        uint32_t cluster_start = data_start + (current_cluster - 2) * bs.sectors_per_cluster * bs.bytes_per_sector;
        fseek(disk, cluster_start, SEEK_SET);

        uint32_t to_read = bs.sectors_per_cluster * bs.bytes_per_sector;
        if (to_read > bytes_remaining) {
            to_read = bytes_remaining;
        }

        char buffer[4096];  // Fixed-size buffer
        size_t bytes_read = fread(buffer, 1, to_read, disk);
        fwrite(buffer, 1, bytes_read, output);

        bytes_remaining -= bytes_read;
        current_cluster = read_fat_entry(disk, &bs, current_cluster);
    }

    fclose(output);
    fclose(disk);
    printf("File copied successfully.\n");
    return 0;
}
