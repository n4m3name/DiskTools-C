# DiskTools-c

FAT12 File System Utilities written in C

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

**Compilation:**  
Use the provided Makefile to compile all utilities:
    `make`
The compiled files can be removed using `make clean`

**Recommended usage:**  
The repository includes the following disks (in the TestDisks folder) for testing, which were given in the course notes for CSC360: Operating Systems:

1. disk1.IMA:
   ```
   .
   ├── ans1.pdf
   ├── Icebergs.tex
   └── Reminder.txt
   ```

2. disk2.IMA:
   ```
   .
   ├── acs.c
   ├── fat12.pdf
   └── SUB1
       └── input1.txt
   ```

3. disk3.IMA:
   ```
   .
   ├── Figure1.jpg
   ├── SUB1
   │   └── SUBSUB1
   │       ├── 2f11.jpg
   │       └── SUBSUB11
   │           └── readme.txt
   └── SUB2
   ```

The most straightforward way to test is to first copy one of the test files to a .IMA file in the project folder, i.e. `cp TestDisks/disk<x>.IMA test.IMA` and proceed to run the programs on `test.IMA`, i.e. `disk`; The code was written using these .IMA files for testing.

Also included is `foo.txt`, a simple text file for use with `diskput.c` and `diskget.c`. The code for these files was also tested using `Figure1.jpg` from disk3.IMA.