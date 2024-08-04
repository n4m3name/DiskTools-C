# DiskTools-c

FAT12 File System Utilities

This package contains four utilities for working with FAT12 file system images:

1. **diskinfo - File System Information Utility**
   Displays general information about the FAT12 file system, including:
   - OS Name
   - Disk Label
   - Total and Free Disk Size
   - Number of Files
   - FAT Information

   Usage: `./diskinfo <disk_image>`

2. **disklist - Directory Listing Utility**
   Lists the contents of the root directory and all subdirectories in the file system.
   Displays file attributes, sizes, names, and creation times.

   Usage: `./disklist <disk_image>`

3. **diskget - File Extraction Utility**
   Copies a specified file from the root directory of the FAT12 file system to the current Linux directory.

   Usage: `./diskget <disk_image> <filename>`

4. **diskput - File Insertion Utility**
   Copies a file from the current Linux directory into a specified directory (root or subdirectory) of the FAT12 file system image.

   Usage: `./diskput <disk_image> [/path/to/]<filename>`

All utilities are designed to work with FAT12 file system images and handle various aspects of the file system structure, including:
- Boot Sector analysis
- FAT (File Allocation Table) manipulation
- Directory entry parsing and creation
- Cluster chain traversal

These tools provide a comprehensive set of operations for examining and modifying FAT12 file systems, useful for both educational purposes and practical file system management tasks.

Compilation:
Use the provided Makefile to compile all utilities:
    `make`
The compiled files can be removed using `make clean`

## Recommended usage:
The repository includes the following disks (in the TestDisks folder) for testing:
- disk1.IMA
- disk2.IMA
- disk3.IMA