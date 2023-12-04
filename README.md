# Rain - File Archiver for the Drop Format

## Overview
`Rain` is a versatile file archiver designed for the `drop` format, capable of handling multiple file system objects efficiently. It offers a range of features suitable for both personal and professional use.

## Features

### Basic Operations (Subset 0)
- **List Path Names**: Enumerates the path names of each object in a drop.
- **List Permissions**: Displays the permissions of each object in a drop.
- **List File Sizes**: Shows the size (in bytes) of files in a drop.
- **Check Droplet Magic Number**: Verifies the droplet magic number for integrity.

### Intermediate Operations (Subset 1)
- **Extract Files**: Allows extraction of files from a drop.
- **Check Integrity**: Checks droplet hashes to ensure the integrity of the drop.
- **Set File Permissions**: Assigns file permissions for files extracted from a drop.

### Advanced Operations (Subset 2)
- **Create Drops**: Facilitates creating a drop from a list of files.

### Enhanced Functionality (Subset 3)
- **Directory Support**: Lists, extracts, and creates drops that include directories.
- **Multi-format Support**: Handles extraction and creation of drops in both 7-bit and 6-bit formats.

## Archive Formats
While `Rain` focuses on the `drop` format, it's worth exploring the vast array of archive formats available. For instance, `tar` is prevalent on *nix-like systems, and `Zip` on Windows. A detailed exploration can be found in [Wikipedia's list of archive formats](https://en.wikipedia.org/wiki/List_of_archive_formats).

## Project Files Overview

### `rain.c`
- **Description**: The primary file requiring modification. It contains partial definitions of four key functions: `list_drop`, `check_drop`, `extract_drop`, and `create_drop`. 

### `rain_main.c`
- **Description**: Contains the `main` function. It includes code to parse command line arguments and calls one of the four main functions (`list_drop`, `extract_drop`, `create_drop`, `check_drop`) based on the arguments.

### `rain.h`
- **Description**: Holds shared function declarations and some useful constant definitions.

### `rain_hash.c`
- **Description**: Contains the `droplet_hash` function.
- **Your Task**: Call this function to calculate hashes for subset 1.

### `rain_6_bit.c`
- **Description**: Includes `droplet_to_6_bit` and `droplet_from_6_bit` functions.
- **Your Task**: Utilize these functions to implement the 6-bit format for subset 3.

### `rain.mk`
- **Description**: Contains a Makefile fragment for the `rain` project.

## Compilation and Execution

### Using `make`

To compile the code, run the `make` command. This will compile the provided code, and you should be able to run the resulting executable.

```bash
make
dcc    -c -o rain.o rain.c
dcc    -c -o rain_main.o rain_main.c
dcc    -c -o rain_hash.o rain_hash.c
dcc    -c -o rain_6_bit.o rain_6_bit.c
dcc   rain.o rain_main.o rain_hash.o rain_6_bit.o   -o rain
./rain -l a.drop
# Output: list_drop called to list drop: 'a.drop'

You can run make to compile the provided code; and you should be able to run the result.
```
If dcc is not available, you can compile the project using gcc by specifying CC=gcc with the make command

```bash
make CC=gcc
gcc    -c -o rain.o rain.c
gcc    -c -o rain_main.o rain_main.c
gcc    -c -o rain_hash.o rain_hash.c
gcc    -c -o rain_6_bit.o rain_6_bit.c
gcc   rain.o rain_main.o rain_hash.o rain_6_bit.o   -o rain
./rain -l a.drop
# Output: list_drop called to list drop: 'a.drop'
```

# The Drop and Droplet Format

## Overview
Drops must adhere to the format produced by the reference implementation. A drop consists of a sequence of one or more droplets, each containing information about a file or directory.

## Droplet Structure
The first byte of a drop file is the first byte of the first droplet. Each droplet is immediately followed by either another droplet or the end of the drop file.

### Droplet Fields


| Name              | Length         | Type                                  | Description                                                                                   |
|-------------------|----------------|---------------------------------------|-----------------------------------------------------------------------------------------------|
| Magic Number      | 1 Byte         | Unsigned, 8-bit, little-endian        | Byte 0 in every droplet must be 0x63 (ASCII 'c').                                             |
| Droplet Format    | 1 Byte         | Unsigned, 8-bit, little-endian        | Byte 1 in every droplet must be one of 0x36, 0x37, 0x38 (ASCII '6', '7', '8').                |
| Permissions       | 10 Bytes       | Characters                            | Bytes 2—11 are the type and permissions as an ls-like character array; e.g., "-rwxr-xr-x".    |
| Pathname Length   | 2 Bytes        | Unsigned, 16-bit, little-endian       | Bytes 12—13 are an unsigned 2-byte integer, giving the length of the pathname.                |
| Pathname          | Pathname Length| Characters                            | The filename of the object in this droplet.                                                   |
| Content Length    | 6 Bytes        | Unsigned, 48-bit, little-endian       | An unsigned 6-byte integer giving the length of the file's data.                              |
| Content           | Varies         | Bytes                                 | The data of the object in this droplet.                                                       |
| Hash              | 1 Byte         | Unsigned, 8-bit, little-endian        | The last byte of a droplet is a droplet-hash of all bytes of this droplet except this byte.   |

## Droplet Content Encodings (Subset 3 only)

### 8-bit Format
- Droplet format == 0x38
- Contents are an array of bytes, equivalent to the bytes in the original file.

### 7-bit Format
- Droplet format == 0x37
- Contents are an array of bytes representing packed seven-bit values, with trailing bits set to zero.
- Needs ⌈ (7.0 / 8) * content-length ⌉ bytes.

### 6-bit Format
- Droplet format == 0x36
- Contents are an array of bytes of packed six-bit values.
- Cannot store all ASCII values, e.g., upper case letters.
- Needs ⌈ (6.0 / 8) * content-length ⌉ bytes.

## Packed n-bit Encoding (Subset 3 only)
Smaller values are often stored in larger types. For example, three seven-bit values (a, b, c) stored in eight-bit variables would be packed as follows:

a: 0b0AAA_AAAA,
b: 0b0BBB_BBBB,
c: 0b0CCC_CCCC,

Packed Encoding: 0bAAAA_AAAB_BBBB_BBCC_CCCC_C000


## Inspecting Drops and Droplets
Use the `hexdump` utility to inspect drops and droplets. For example:

```bash
hexdump -vC examples/2_files.drop
```
This command will display the hex and ASCII values of each byte in the file. Each line of output includes an address column, data columns, and a human-readable stripe.

# The Droplet Hash (Subsets 1, 2, 3)

## Overview
Each droplet ends with a hash (or digest) which is calculated from the other values of the droplet. This hash helps in detecting if any bytes of the drop have changed, potentially due to disk or network errors.

## Droplet Hash Function
The `droplet_hash()` function computes one step of the hash for a sequence of bytes:

```c
uint8_t droplet_hash(uint8_t current_hash_value, uint8_t byte_value) {
    return ((current_hash_value * 33) & 0xff) ^ byte_value;
}
```

This function takes the hash value of the sequence up to the current byte, and the value of this byte, to calculate the new hash value.

### Example

Creating a drop of a single one-byte file (echo >a) and then compressing it into a drop (1521 rain -c a.drop a), we can inspect the drop to see its hash is 0x15.

hexdump -Cv a.drop

This results in the following output, showing the hash at the end:

00000000  63 38 2d 72 77 2d 72 2d  2d 72 2d 2d 01 00 61 01  |c8-rw-r--r--..a.|
00000010  00 00 00 00 00 0a 15                              |.......|
00000017

### Calculating Hash
Here is the sequence of calls that calculated the hash value of 0x15:

droplet_hash(0x00, 0x63) = 0x63
droplet_hash(0x63, 0x38) = 0xfb
...
droplet_hash(0x3f, 0x0a) = 0x15

This sequence illustrates how the droplet_hash function iteratively processes each byte of the droplet to arrive at the final hash value.

# Usage
```bash
rain [<FORMAT>] <MODE> <ARCHIVE-FILE> [<FILE...>]
```

## Common Modes

- **List (-l, --list)**  
  List basic information about all files in `ARCHIVE-FILE`.

- **List Long (-L, --list-long)**  
  List additional information about all files in `ARCHIVE-FILE`.

- **Check (-C, --check)**  
  Check the hash of `ARCHIVE-FILE`.

- **Create (-c, --create)**  
  Create `ARCHIVE-FILE` containing the listed files.

- **Append (-a, --append)**  
  Append the listed files to `ARCHIVE-FILE`.

- **Extract (-x, --extract)**  
  Extract all files from `ARCHIVE-FILE`.

## Common Formats

- **6-bit Format (-6)**  
  Create or append to `ARCHIVE-FILE` using 6-bit format.

- **7-bit Format (-7)**  
  Create or append to `ARCHIVE-FILE` using 7-bit format.

- **8-bit Format (-8)**  
  Create or append to `ARCHIVE-FILE` using 8-bit format (default).

### Examples

- To list files in an archive: `rain -l archive.drop`
- To create an archive in 7-bit format with files `file1.txt` and `file2.txt`: `rain -7 -c archive.drop file1.txt file2.txt`
- To append `file3.txt` to an existing archive in 6-bit format: `rain -6 -a archive.drop file3.txt`
- To extract all files from an archive: `rain -x archive.drop`