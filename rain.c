#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>

#include "rain.h"

#define VALID_MAGIC_NUMBER 0x63
#define DROPLET_FMT_6 0x36
#define DROPLET_FMT_7 0x37
#define DROPLET_FMT_8 0x38
#define MAGIC_NUMBER_BYTES 1
#define DROPLET_FORMAT_BYTES 1
#define PERMISSIONS_BYTES 10
#define PATHNAME_LENGTH_BYTES 2
#define CONTENT_LENGTH_BYTES 6
#define HASH_BYTES 1
#define BYTE_SIZE 8
#define FORMAT_7_BYTES 7
#define FORMAT_8_BYTES 8
#define FORMAT_6_BYTES 6

uint8_t calculate_hash(long droplet_length, FILE *input_stream);
mode_t convert_permissions_array(char *permissions);
char *convert_permissions_to_array(mode_t mode);
long create_drop_recursive(FILE *output_stream, int format, char *pathname, long amount_of_bytes);
long create_directory_droplet(FILE *output_stream, int format, char *pathname, long amount_of_bytes);
long create_file_droplet(FILE *output_stream, int format, char *pathname, long amount_of_bytes);
long create_drop_backwards(FILE *output_stream, int format, char *pathname, long amount_of_bytes);
void extract_7_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length);
void extract_8_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length);
void extract_6_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length);
uint64_t create_file(char *pathname, mode_t mode, FILE *input_stream, uint8_t format, uint64_t content_length);
void create_directory(char *pathname, mode_t mode);
int fgetc_with_EOF_checking(FILE *input_stream);


// print the files & directories stored in drop_pathname (subset 0)
// if long_listing is non-zero then file/directory permissions, formats & sizes 
// are also printed (subset 0)

void list_drop(char *drop_pathname, int long_listing) {
    FILE *input_stream = fopen(drop_pathname, "rb");
    if (input_stream == NULL) {
        perror(drop_pathname);
        exit(1);
    }
    int byte;
    long amount_of_bytes = 0;
    while ((byte = fgetc(input_stream)) != EOF) {
        // on 2nd byte

        byte = fgetc_with_EOF_checking(input_stream);
        uint8_t format = byte;
        
        char permissions[PERMISSIONS_BYTES + 1];
        for (int i = 0; i < PERMISSIONS_BYTES; i++) {
            permissions[i] = fgetc_with_EOF_checking(input_stream);
        }
        permissions[PERMISSIONS_BYTES] = '\0';

        byte = fgetc_with_EOF_checking(input_stream);  
        int byte2 = fgetc_with_EOF_checking(input_stream);  
        // access 2 bytes as a single uint16_t value
        // little endian
        uint16_t pathname_length = (byte2 << BYTE_SIZE) | byte;

        char pathname[pathname_length + 1];
        for (int i = 0; i < pathname_length; i++) {
            pathname[i] = fgetc_with_EOF_checking(input_stream);
        }
        pathname[pathname_length] = '\0';


        // get content length
        uint64_t content_length = 0;
        // Read 6 bytes from input stream
        for (int i = 0; i < CONTENT_LENGTH_BYTES; i++) {
            byte = fgetc_with_EOF_checking(input_stream);
            content_length |= ((uint64_t)byte << (i * BYTE_SIZE));
        }

        // print to stdout
        if (long_listing) {
            printf("%s  %c  %5lu  %s\n", permissions, format, content_length, pathname);
        } else {
            printf("%s\n", pathname);
        }

        // account for format
        if (format == DROPLET_FMT_7) {
            content_length = (uint64_t) ceil(((double)FORMAT_7_BYTES / 
                (double)FORMAT_8_BYTES) * (double) content_length);
        } else if (format == DROPLET_FMT_6) {
            content_length = (uint64_t) ceil(((double)FORMAT_6_BYTES / 
                (double)FORMAT_8_BYTES) * (double) content_length);
        }

        // error check unchecked bytes
        for (int i = 0; i < (content_length + HASH_BYTES); i++) {
            fgetc_with_EOF_checking(input_stream);
        }

        // add altogether to change amount of bytes
        // so the processs can repeat until EOF is found
        amount_of_bytes = amount_of_bytes + MAGIC_NUMBER_BYTES + 
            DROPLET_FORMAT_BYTES + 
            PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length +
            CONTENT_LENGTH_BYTES +
            content_length + HASH_BYTES;
        
        // go to next droplet
        fseek(input_stream, amount_of_bytes, SEEK_SET);
    }
    if (fclose(input_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }
}


// check the files & directories stored in drop_pathname (subset 1)
// prints the files & directories stored in drop_pathname with a message
// either, indicating the hash byte is correct, or
// indicating the hash byte is incorrect, what the incorrect value is and the 
// correct value would be

void check_drop(char *drop_pathname) {
    // check the first byte of the droplet for c if its not  eof
    FILE *input_stream = fopen(drop_pathname, "rb");
    if (input_stream == NULL) {
        perror(drop_pathname);
        exit(1);
    }

    int byte;
    long amount_of_bytes = 0;
    while ((byte = fgetc(input_stream)) != EOF) {
        // on 2nd byte
        // check byte 1 to see if 0x63 (magic number)
        if (byte != VALID_MAGIC_NUMBER) {
            fprintf(stderr, "error: incorrect first droplet byte: 0x%02x should be 0x63\n", byte);
            exit(1);
        }

        // check format
        byte = fgetc_with_EOF_checking(input_stream);
        uint8_t format = byte;
        if (!(format == DROPLET_FMT_6 || format == DROPLET_FMT_7 || format == DROPLET_FMT_8)) {
            fprintf(stderr, "error: droplet format is wrong\n");
            exit(1);
        }
        
        // error check unchecked bytes
        for (int i = 0; i < PERMISSIONS_BYTES; i++) {
            fgetc_with_EOF_checking(input_stream);
        }

        byte = fgetc_with_EOF_checking(input_stream);  
        int byte2 = fgetc_with_EOF_checking(input_stream);  
        // access 2 bytes as a single uint16_t value
        // little endian
        uint16_t pathname_length = (byte2 << BYTE_SIZE) | byte;
        
        char pathname[pathname_length + 1];
        for (int i = 0; i < pathname_length; i++) {
            pathname[i] = fgetc_with_EOF_checking(input_stream);
        }
        pathname[pathname_length] = '\0';

        // get content length
        uint64_t content_length = 0;
        // Read 6 bytes from input stream
        for (int i = 0; i < CONTENT_LENGTH_BYTES; i++) {
            byte = fgetc_with_EOF_checking(input_stream);
            content_length |= ((uint64_t)byte << (i * BYTE_SIZE));
        }

        // account for format
        if (format == DROPLET_FMT_7) {
            content_length = (uint64_t) ceil(((double)FORMAT_7_BYTES / 
                (double)FORMAT_8_BYTES) * (double) content_length);
        } else if (format == DROPLET_FMT_6) {
            content_length = (uint64_t) ceil(((double)FORMAT_6_BYTES / 
                (double)FORMAT_8_BYTES) * (double) content_length);
        }

        // go back to the start and check the hashing now
        fseek(input_stream, amount_of_bytes, SEEK_SET);

        long droplet_length = MAGIC_NUMBER_BYTES + DROPLET_FORMAT_BYTES + 
            PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length + 
            CONTENT_LENGTH_BYTES +
            content_length + HASH_BYTES;

        amount_of_bytes = amount_of_bytes + droplet_length;

        // check hash
        uint8_t calculated_hash = calculate_hash(droplet_length - 1, input_stream);
        byte = fgetc_with_EOF_checking(input_stream);
        if (calculated_hash != byte) {
            printf("%s - incorrect hash 0x%02x should be 0x%02x\n", 
                pathname, calculated_hash, byte);
        } else {
            printf("%s - correct hash\n", pathname);
        }

        // already at end of the drop
    }
    if (fclose(input_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }
}


// extract the files/directories stored in drop_pathname (subset 2 & 3)
void extract_drop(char *drop_pathname) {
    FILE *input_stream = fopen(drop_pathname, "rb");
    if (input_stream == NULL) {
        perror(drop_pathname);
        exit(1);
    }
    int byte;
    long amount_of_bytes = 0;

    while ((byte = fgetc(input_stream)) != EOF) {
        // on 2nd byte
        byte = fgetc_with_EOF_checking(input_stream);
        uint8_t format = byte;
        
        char permissions[PERMISSIONS_BYTES + 1];
        for (int i = 0; i < PERMISSIONS_BYTES; i++) {
            permissions[i] = fgetc_with_EOF_checking(input_stream);
        }
        permissions[PERMISSIONS_BYTES] = '\0';

        byte = fgetc_with_EOF_checking(input_stream);  
        int byte2 = fgetc_with_EOF_checking(input_stream);  
        // access 2 bytes as a single uint16_t value
        // little endian
        uint16_t pathname_length = (byte2 << BYTE_SIZE) | byte;

        char pathname[pathname_length + 1];
        for (int i = 0; i < pathname_length; i++) {
            pathname[i] = fgetc_with_EOF_checking(input_stream);
        }
        pathname[pathname_length] = '\0';

        // get content length
        uint64_t content_length = 0;
        // Read 6 bytes from input stream
        for (int i = 0; i < CONTENT_LENGTH_BYTES; i++) {
            byte = fgetc_with_EOF_checking(input_stream);
            content_length |= ((uint64_t)byte << (i * BYTE_SIZE));
        }

        // check if file or directory
        mode_t mode = convert_permissions_array(permissions);
        if (mode & S_IFDIR) {
            create_directory(pathname, mode);
            // add altogether to change amount of bytes
            // so the processs can repeat until EOF is found

            amount_of_bytes = amount_of_bytes + MAGIC_NUMBER_BYTES + 
                DROPLET_FORMAT_BYTES + 
                PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length + 
                CONTENT_LENGTH_BYTES +
                content_length + HASH_BYTES;
            
            // go to next droplet
            fseek(input_stream, amount_of_bytes, SEEK_SET);
        } else {
            content_length = create_file(pathname, mode, input_stream, format, content_length);
            // add altogether to change amount of bytes
            // so the processs can repeat until EOF is found
            amount_of_bytes = amount_of_bytes + MAGIC_NUMBER_BYTES + 
            DROPLET_FORMAT_BYTES + 
            PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length + 
            CONTENT_LENGTH_BYTES +
            content_length + HASH_BYTES;
            
            // go to next droplet
            fseek(input_stream, amount_of_bytes, SEEK_SET);
        }

        // check the hash byte for EOF
        fseek(input_stream, -1, SEEK_CUR);
        fgetc_with_EOF_checking(input_stream);
    }

    if (fclose(input_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }
}

// tries to create direcotry and/or set permissions
void create_directory(char *pathname, mode_t mode) {
    printf("Creating directory: %s\n", pathname);
    if (mkdir(pathname, mode) != 0) {
        if (errno == EEXIST) {
            // Directory exists 
            if (chmod(pathname, mode) != 0) {
                perror(pathname);
                exit(1);
            }
        }
        else {
            perror(pathname);
            exit(1);
        }
    }
}

// creates file of specified format with specified permissions
uint64_t create_file(char *pathname, mode_t mode, FILE *input_stream, uint8_t format, uint64_t content_length) {
    printf("Extracting: %s\n", pathname); 
    // up to content section on input stream
    // open output stream
    FILE *output_stream = fopen(pathname, "w"); 
    if (output_stream == NULL) {
        perror(pathname);
        exit(1);
    }

    // set permissions
    if (chmod(pathname, mode) != 0) {
        perror(pathname);
        exit(1);
    }
    
    // account for format
    if (format == DROPLET_FMT_7) {
        extract_7_bits(input_stream, output_stream, content_length);
        content_length = (uint64_t) ceil(((double)FORMAT_7_BYTES / 
            (double)FORMAT_8_BYTES) * (double) content_length);
    } else if (format == DROPLET_FMT_6) {
        extract_6_bits(input_stream, output_stream, content_length);
        content_length = (uint64_t) ceil(((double)FORMAT_6_BYTES / 
            (double)FORMAT_8_BYTES) * (double) content_length);
    } else if (format == DROPLET_FMT_8) {
        extract_8_bits(input_stream, output_stream, content_length);
    } else {
        perror("invalid fomrat type");
    }

    if (fclose(output_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }

    return content_length;
}


// create drop_pathname containing the files or directories specified in 
// pathnames (subset 3)
// if append is zero drop_pathname should be over-written if it exists
// if append is non-zero droplets should be instead appended to drop_pathname 
// if it exists
// format specifies the droplet format to use, it must be one DROPLET_FMT_6,
// DROPLET_FMT_7 or DROPLET_FMT_8

void create_drop(char *drop_pathname, int append, int format,
    int n_pathnames, char *pathnames[n_pathnames]) {
    // create drop
    FILE *output_stream;
    if (append) {
        output_stream = fopen(drop_pathname, "ab+");
    } else {
        output_stream = fopen(drop_pathname, "wb+");
    }
    if (output_stream == NULL) {
        perror(drop_pathname);
        exit(1);
    }
    struct stat stats;
    if (stat(drop_pathname, &stats) != 0) {
        perror(drop_pathname);
        exit(1);
    }
    long amount_of_bytes = (long)stats.st_size;
    
    for (int i = 0; i < n_pathnames; i++) {
        char *pathname = strdup(pathnames[i]);
        amount_of_bytes = create_drop_backwards(output_stream, format, pathname, amount_of_bytes);
        free(pathname);
        amount_of_bytes = create_drop_recursive(output_stream, format, pathnames[i], amount_of_bytes);
    }

    if (fclose(output_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }
}

// a copy of pathname needs to be made as strtok changes orignal string
// goes back through the pathname and puts any directories inlcuded in it into 
// the drop file 
long create_drop_backwards(FILE *output_stream, int format, char *pathname, long amount_of_bytes) {
    // have to use malloc for vairable lenght arrays, length is always going ot 
    // be less thne pathname length 
    char *previous_pathname = malloc(strlen(pathname));
    char *new_dir = strtok(pathname, "/");
    char *next_path = strtok(NULL, "/");
    bool first = true;
    // check if its the last directory or file in pathname by seeing if 
    // next_path is null
    while (next_path != NULL) {
        // add the new directory and call funciton on it then after add /
        if (first) {
            strcpy(previous_pathname, new_dir);
            first = false;
        } else {
            strcat(previous_pathname, new_dir);
        }
        amount_of_bytes = create_directory_droplet(output_stream, format, previous_pathname, amount_of_bytes);
        strcat(previous_pathname, "/");
        // automatically adds null terminator
        new_dir = next_path;
        next_path = strtok(NULL, "/");
    }
    free(previous_pathname);
    return amount_of_bytes;
}

// recursive function that gets called on every single pathname from the original
// pathnames given to create_drop
// check if its file or directory so appropriate actions can be done
long create_drop_recursive(FILE *output_stream, int format, char *pathname, long amount_of_bytes) {
    struct stat stats;
    if (stat(pathname, &stats) != 0) {
        perror(pathname);
        exit(1); 
    }
    if (stats.st_mode & S_IFDIR) {
        // directory
        amount_of_bytes = create_directory_droplet(output_stream, format, pathname, amount_of_bytes);
        DIR *dir = opendir(pathname);
        if (dir == NULL) {
            perror(pathname);
            exit(1);
        }
        struct dirent *entry;

        // process each entry in the directory
        while ((entry = readdir(dir)) != NULL) {
            // ignore entries for the current and parent directories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char *sub_dir_path = malloc(strlen(pathname) + 1 + strlen(entry->d_name) + 1);
            // copy the first string into the destination buffer
            strcpy(sub_dir_path, pathname); 
            strcat(sub_dir_path, "/");
            // append the second string
            strcat(sub_dir_path, entry->d_name); 
            // automatically adds null terminator
            amount_of_bytes = create_drop_recursive(output_stream, format, sub_dir_path, amount_of_bytes);
            free(sub_dir_path);
        }
        if (closedir(dir) == -1) {
            perror("Failed to close directory");
            exit(1);
        }
    } else {
        amount_of_bytes = create_file_droplet(output_stream, format, pathname, amount_of_bytes);
    }

    return amount_of_bytes;
}


// writes to the drop file if droplet to be wrote is a directory
// returns amount of bytes so position can be recoreded in drop_pathname
// file pointer is a the start of the next droplet
long create_directory_droplet(FILE *output_stream, int format, char *pathname, long amount_of_bytes) {
    printf("Adding: %s\n", pathname);
    int byte;

    //go to end of drop file to add new droplet
    fseek(output_stream, amount_of_bytes, SEEK_SET);

    // add magic number and format
    byte = VALID_MAGIC_NUMBER;
    fputc(byte, output_stream);
    fputc(format, output_stream);

    struct stat stats;
    //get permissions from 
    if (stat(pathname, &stats) != 0) {
        perror(pathname);
        exit(1); 
    }
    char *permissions = convert_permissions_to_array(stats.st_mode);

    // print to permssions file 
    for (int j = 0; j < PERMISSIONS_BYTES; j++) {
        fputc(permissions[j], output_stream);
    }
    free(permissions);

    // get pathname length and print to file
    // doesnt inlclude the null terminator
    size_t pathname_length = strlen(pathname);
    // little endian so print smallest bits first
    fputc(pathname_length, output_stream);
    fputc((pathname_length >> 8), output_stream);

    // print pathname which doesnt inlcude the null terminator
    for (int j = 0; j < pathname_length; j++) {
        fputc(pathname[j], output_stream);
    }
    // its a directory so content length is 0
    uint64_t content_length = 0;

    // print content length bytes to file
    // little endian so smallest bits first
    for (int j = 0; j < CONTENT_LENGTH_BYTES; j++) {
        byte = (content_length >> ((j * BYTE_SIZE) & 0xFF));
        fputc(byte, output_stream);
    }

    // go back to the start and check the hashing now
    fseek(output_stream, amount_of_bytes, SEEK_SET);

    long droplet_length = MAGIC_NUMBER_BYTES + DROPLET_FORMAT_BYTES + 
    PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length + 
    CONTENT_LENGTH_BYTES +
    content_length + HASH_BYTES;

    amount_of_bytes = amount_of_bytes + droplet_length;

    fputc(calculate_hash(droplet_length - 1, output_stream), output_stream);
    // at the end of the drop

    return amount_of_bytes;
}

// writes to the drop file if droplet to be wrote is a file not directory
// returns amount of bytes so position can be recoreded in drop_pathname
// file pointer is a the start of the next droplet
long create_file_droplet(FILE *output_stream, int format, char *pathname, long amount_of_bytes) {
    FILE *input_stream = fopen(pathname, "rb");
    if (input_stream == NULL) {
        perror(pathname);
        exit(1);
    }
    printf("Adding: %s\n", pathname);
    int byte;

    //go to end of drop file to add new droplet
    fseek(output_stream, amount_of_bytes, SEEK_SET);

    // add magic number and format
    byte = VALID_MAGIC_NUMBER;
    fputc(byte, output_stream);
    fputc(format, output_stream);
    
    struct stat stats;
    //get permissions from file
    if (stat(pathname, &stats) != 0) {
        perror(pathname);
        exit(1); 
    }
    char *permissions = convert_permissions_to_array(stats.st_mode);

    // print to permssions file 
    for (int j = 0; j < PERMISSIONS_BYTES; j++) {
        fputc(permissions[j], output_stream);
    }
    free(permissions);

    // get pathname length and print to file
    // doesnt inlclude the null terminator
    size_t pathname_length = strlen(pathname);
    // little endian so print smallest bits first
    fputc(pathname_length, output_stream);
    fputc((pathname_length >> 8), output_stream);

    // print pathname which doesnt inlcude the null terminator
    for (int j = 0; j < pathname_length; j++) {
        fputc(pathname[j], output_stream);
    }
    
    // get content length and print to file
    uint64_t content_length = (long)stats.st_size;
    
    // little endian so smallest bits first
    for (int j = 0; j < CONTENT_LENGTH_BYTES; j++) {
        byte = (content_length >> ((j * BYTE_SIZE) & 0xFF));
        fputc(byte, output_stream);
    }
    
    // print content to file
    fseek(input_stream, 0, SEEK_SET);
    for (int j = 0; j < content_length; j++) {
        byte = fgetc(input_stream);
        fputc(byte, output_stream);
    }

    // go back to the start and check the hashing now
    fseek(output_stream, amount_of_bytes, SEEK_SET);

    long droplet_length = MAGIC_NUMBER_BYTES + DROPLET_FORMAT_BYTES + 
    PERMISSIONS_BYTES + PATHNAME_LENGTH_BYTES + pathname_length + 
    CONTENT_LENGTH_BYTES +
    content_length + HASH_BYTES;

    amount_of_bytes = amount_of_bytes + droplet_length;

    fputc(calculate_hash(droplet_length - 1, output_stream), output_stream);
    // at the end of the drop

    if (fclose(input_stream) != 0) {
        fprintf(stderr, "error: problem encounted with fclose\n");
        exit(1);
    }

    return amount_of_bytes;
}

// calcualtes the final has value of a droplet by scanning through droplet
// droplet legnth is actually the real droplet legnth - 1
// assumes its at the start of the droplet
uint8_t calculate_hash(long droplet_length, FILE *input_stream) {
    uint8_t current_hash_value = 0;
    for (int i = 0; i < droplet_length; i++) {
        current_hash_value = droplet_hash(current_hash_value, fgetc_with_EOF_checking(input_stream));
    }
    return current_hash_value;
}

// converts a permissions array containing 
mode_t convert_permissions_array(char *permissions) {
    // convert the permissions string to an integer in octal mode
    mode_t mode = 0;
    if (permissions[0] == 'd') {
        mode |= S_IFDIR;
    }
    for (int i = 1; i < PERMISSIONS_BYTES; i++) {
        if (permissions[i] == '-') {
            // skip hyphen
            continue;
        } else if (permissions[i] == 'r' || permissions[i] == 'w' || permissions[i] == 'x') {
            // set corresponding permission bit
            mode |= (1 << (8 - (i - 1)));
        } else {
            // invalid character
            fprintf(stderr, "error: invalid input: '%c' is not a valid permission character.\n", permissions[i]);
            exit(1);
        }
    }
    return mode;
}

// converts a permissions of a file to a permissions array containing characters
// returned array is malloc'd and needs to be freed by another function]
// not null terminated
char *convert_permissions_to_array(mode_t mode) {
    char *permissions;
    permissions = malloc(PERMISSIONS_BYTES * sizeof(char));
    // directory or not
    permissions[0] = (mode & S_IFDIR) ? 'd' : '-';
    // owner permissions (2-4th elements)
    permissions[1] = (mode & S_IRUSR) ? 'r' : '-';
    permissions[2] = (mode & S_IWUSR) ? 'w' : '-';
    permissions[3] = (mode & S_IXUSR) ? 'x' : '-';
    // group permissions (5-7th elements)
    permissions[4] = (mode & S_IRGRP) ? 'r' : '-';
    permissions[5] = (mode & S_IWGRP) ? 'w' : '-';
    permissions[6] = (mode & S_IXGRP) ? 'x' : '-';
    // other permissions (last three elements)
    permissions[7] = (mode & S_IROTH) ? 'r' : '-';
    permissions[8] = (mode & S_IWOTH) ? 'w' : '-';
    permissions[9] = (mode & S_IXOTH) ? 'x' : '-';

    return permissions;
}

// gets 8 bits values from input stream and prints to output stream
void extract_8_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length) {
    uint8_t byte;
    for (int i = 0; i < content_length; i++) {
        byte = fgetc_with_EOF_checking(input_stream);
        fputc(byte, output_stream);
    }
}

// doesnt error check for if its not in 7 bit format
// just extracts content_length amount of bytes from input stream
// converts those 7 bit values to 8 bit values
// prints 8 bit values to output stream
void extract_7_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length) {
    uint8_t byte;
    // only has to be over 14 bits
    uint16_t bit_mask; 
    // basically a buffer
    uint16_t bits = 0; 
    int amount_of_bits_in_bits_variable = 0;
    int count = 0;
    while (count < content_length) {
        // get next byte
        byte = fgetc(input_stream);
        
        // add to bits varibale (needs to be inserted at the lsb)
        // thus move over  existing bits by 8 to add new scanned byte
        bits = (bits << BYTE_SIZE) | byte;
        amount_of_bits_in_bits_variable = amount_of_bits_in_bits_variable + BYTE_SIZE;
        
        // now get the leading 7 bits to get the next byte to print out
        byte = (bits >> (amount_of_bits_in_bits_variable - FORMAT_7_BYTES));
        // byte should now have leading 0 added as well
        // so now its converted to 8 bit format
        fputc(byte, output_stream);
        count++;

        //remove those 7 used byte bits from bits
        amount_of_bits_in_bits_variable = amount_of_bits_in_bits_variable - FORMAT_7_BYTES;
        
        // create new bit mask of amount_of_bits_in_bits_variable length
        // after adjusting for the now not needed leading 7 bits
        bit_mask = 0;
        for (int j = 0; j < amount_of_bits_in_bits_variable; j++) {
            bit_mask |= (1 << j);
        }
        bits = (bits & bit_mask);
        
        // eventually there is going to be 7 leftover bits in the bits array and 
        // thus another 7 bits will need to be printed or eventuallly there
        // will be buffer overflow for bits variabel
        if (amount_of_bits_in_bits_variable == FORMAT_7_BYTES && count < content_length) {
            // print leftover bits
            fputc(bits, output_stream);
            count++;
            // update these changes
            bits = 0; 
            amount_of_bits_in_bits_variable = 0;
        }
    }
}

// error checks for if its in 6 bit format
// just extracts content_length amount of bytes from input stream
// converts those 6 bit values to 8 bit values
// prints 8 bit values to output stream
void extract_6_bits(FILE *input_stream, FILE *output_stream, uint64_t content_length) {
    uint8_t byte;
    // only has to be over 12 bits
    uint16_t bit_mask; 
    // basically a buffer
    uint16_t bits = 0; 
    int amount_of_bits_in_bits_variable = 0;
    int converted_byte;
    int count = 0;
    while (count < content_length) {
        // get next byte
        byte = fgetc(input_stream);
        
        // add to bits varibale (needs to be inserted at the lsb)
        // thus move over existing bits by 8 to add new scanned byte
        bits = (bits << BYTE_SIZE) | byte;
        amount_of_bits_in_bits_variable = amount_of_bits_in_bits_variable + BYTE_SIZE;
        
        // now get the leading 6 bits to get the next byte to print out
        byte = (bits >> (amount_of_bits_in_bits_variable - FORMAT_6_BYTES));
        // byte should now have leading 0 added as well
        
        // check if its a valid conversion
        converted_byte = droplet_from_6_bit(byte);
        if (converted_byte == -1) {
            fprintf(stderr, "error: 6 bit value 0x%02x is not valid\n", byte);
            exit(1);
        }
        fputc(converted_byte, output_stream);
        count++;

        //remove those 6 used byte bits from bits
        amount_of_bits_in_bits_variable = amount_of_bits_in_bits_variable - FORMAT_6_BYTES;
        
        // create new bit mask of amount_of_bits_in_bits_variable length
        // after adjusting for the now not needed leading 6 bits
        bit_mask = 0;
        for (int j = 0; j < amount_of_bits_in_bits_variable; j++) {
            bit_mask |= (1 << j);
        }
        bits = (bits & bit_mask);
        
        // eventually there is going to be 6 leftover bits in the bits array and 
        // thus another 6 bits will need to be printed or eventuallly there
        // will be buffer overflow for bits variabel
        if (amount_of_bits_in_bits_variable == FORMAT_6_BYTES && count < content_length) {
            // print leftover bits
            fputc(bits, output_stream);
            count++;
            // update these changes
            bits = 0; 
            amount_of_bits_in_bits_variable = 0;
        }
    }
}

// fgets but produces error if EOF is encounted
int fgetc_with_EOF_checking(FILE *input_stream) {
    int byte = fgetc(input_stream);
    if (byte == EOF) {
        perror("partially created droplet/EOF found");
        exit(1);
    } else {
        return byte;
    }
}
