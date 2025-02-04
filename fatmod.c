#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <linux/msdos_fs.h>

#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes
#define MAX_FILENAME_LENGTH 256
#define NUM_CLUSTERS 1024
#define FAT_SIZE 1020 * 512


// struct msdos_dir_entry {
//         __u8        name[MSDOS_NAME];/* name and extension */
//         __u8        attr;                /* attribute bits */
//         __u8    lcase;                /* Case for base and extension */
//         __u8        ctime_cs;        /* Creation time, centiseconds (0-199) */
//         __le16        ctime;                /* Creation time */
//         __le16        cdate;                /* Creation date */
//         __le16        adate;                /* Last access date */
//         __le16        starthi;        /* High 16 bits of cluster in FAT32 */
//         __le16        time,date,start;/* time, date and first cluster */
//         __le32        size;                /* file size (in bytes) */
// };

struct DirectoryEntry {
    unsigned char name[8];
    unsigned char extention[3];
    unsigned char attribute[1];
    u_int8_t mscreation[2];
    u_int8_t datecreation[4];
    u_int8_t lastAccessDate[2];
    u_int16_t firstclusterhigh;
    u_int8_t lastWriteDate[4];
    u_int16_t firstclusterlow;
    u_int32_t fileSize;
};

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
void listFiles(const char *diskname);
void readFileAscii(const char *diskname, const char *filename);
void readFileBinary(const char *diskname, const char *filename);
void createFile(const char *diskname, const char *filename);
void deleteFile(const char *diskname, const char *filename);
void writeFile(const char *diskname, const char *filename, unsigned int offset, unsigned int n, unsigned char data);
void printHelp();
char *trim(char *s);
u_int32_t getFirstCluster(const struct DirectoryEntry *entry);

int readsector (int fd, unsigned char *buf,unsigned int snum)
{
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = read (fd, buf, SECTORSIZE);
	if (n == SECTORSIZE)
	    return (0);
	else
        return (-1);
}

u_int32_t getFirstCluster(const struct DirectoryEntry *entry) { //returns first cluster of a file
    u_int16_t high = ((u_int16_t)entry->firstclusterhigh);
    u_int16_t low =  ((u_int16_t)entry->firstclusterlow);
    u_int32_t cluster = ((u_int32_t)high << 16) | low;
    return cluster & 0x0FFFFFFF;  // Ensure only 28 bits are used
}

int isValidEntry(const struct DirectoryEntry *entry) { //returns true if valid entry false if empty
    char name[9];
    strncpy(name, (char*)entry->name, 8);
    name[8] = '\0';

    char ext[4];
    strncpy(ext, (char*)entry->extention, 3);
    ext[3] = '\0';


    int n = strlen(trim(name)) + strlen(trim(ext));

    if(n == 0)
        return FALSE;
    return TRUE;
}

u_int32_t findFirstEmptyCluster(int fd) {
    u_int32_t table [FAT_SIZE];
   
	lseek (fd, 512 * 32, SEEK_SET);
	int n  = read (fd, table, 1020*512);
	if (n != FAT_SIZE){
        printf("Returned Cluster is -1 fat read error");
        return -1; 
    }

    for (u_int32_t cluster = 2; cluster < FAT_SIZE; cluster++) {  // Start from cluster 2
        if (table[cluster] == 0) {
            printf("First empty cluster requested, returned Cluster = %d\n", cluster);
            return cluster;  // Found an empty cluster
        }
    }
    return -1;  // No empty cluster found
}

void parseFilename(const char *filename, char name[8], char ext[3]) {//seperates ext and name given a filename
    memset(name, ' ', sizeof(name));
    memset(ext, ' ', sizeof(ext));

    const char *dotPos = strrchr(filename, '.');

    if (dotPos != NULL) {
        strncpy(name, filename, dotPos - filename);
        strncpy(ext, dotPos + 1, 3);
    } else {
        strncpy(name, filename, sizeof(name) - 1); 
    }
}

char *trim(char *s) { //trims the string used to compare strings
    char *ptr;
    if (!s)
        return NULL;
    if (!*s)
        return s;
    for (ptr = s + strlen(s) - 1; (ptr >= s) && isspace(*ptr); --ptr);
    ptr[1] = '\0';
    return s;
}

u_int32_t getNextCluster(int fd, u_int32_t clusterNo) { //using the fat table finds the next cluster
    u_int32_t table [FAT_SIZE];
   
	lseek (fd, 512 * 32, SEEK_SET);
	int n  = read (fd, table, 1020*512);
	if (n == FAT_SIZE){
        return (u_int32_t) table[clusterNo]; 
    }
	else
        return (-1);
}



int writesector (int fd, unsigned char *buf, unsigned int snum)
{
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = write (fd, buf, SECTORSIZE);
    fsync (fd);
    if (n == SECTORSIZE)
	     return (0);
	else
        return (-1);
}

int readcluster (int fd, unsigned char *buf,unsigned int cnum)
{
	off_t offset;
	int n;
	offset = (32 * SECTORSIZE) + FAT_SIZE + ((cnum-2) * CLUSTERSIZE);
	lseek (fd, offset, SEEK_SET);
	n  = read (fd, buf, CLUSTERSIZE);
    //printf("Read cluster called read bytes%d.\n", n);

	if (n == CLUSTERSIZE)
	    return (0);
	else
        return (-1);
}


int writecluster (int fd, unsigned char *buf, unsigned int cnum)
{
	off_t offset;
	int n;
	offset = (32 * SECTORSIZE) + FAT_SIZE + ((cnum-2) * CLUSTERSIZE );
	lseek (fd, offset, SEEK_SET);
	n  = write (fd, buf, CLUSTERSIZE);
    fsync (fd);
    if (n == CLUSTERSIZE)
	     return (0);
	else
        return (-1);
}


int main(int argc, char *argv[]) {


    if (argc < 3) {
        fprintf(stderr, "Usage: %s DISKIMAGE OPTION [PARAMETERS]\n", argv[0]);
        return 1;
    }

    char diskname[MAX_FILENAME_LENGTH];
    strcpy(diskname, argv[1]);
    char *option = argv[2];

    if (strcmp(option, "-l") == 0) {
        listFiles(diskname);
    } else if (strcmp(option, "-r") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: Missing parameters for option -r\n");
            return 1;
        }
        char *readOption = argv[3];
        char *filename = argv[4];
        if (strcmp(readOption, "-a") == 0) {
            readFileAscii(diskname, filename);
        } else if (strcmp(readOption, "-b") == 0) {
            readFileBinary(diskname, filename);
        } else {
            fprintf(stderr, "Error: Invalid read option\n");
            return 1;
        }
    } else if (strcmp(option, "-c") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Missing filename for option -c\n");
            return 1;
        }
        char *filename = argv[3];
        createFile(diskname, filename);
    } else if (strcmp(option, "-d") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Missing filename for option -d\n");
            return 1;
        }
        char *filename = argv[3];
        deleteFile(diskname, filename);
    } else if (strcmp(option, "-w") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Error: Missing parameters for option -w\n");
            return 1;
        }
        char *filename = argv[3];
        unsigned int offset = atoi(argv[4]);
        unsigned int n = atoi(argv[5]);
        unsigned char data = (unsigned char)atoi(argv[6]);
        writeFile(diskname, filename, offset, n, data);
    } else if (strcmp(option, "-h") == 0) {
        printHelp();
    } else {
        fprintf(stderr, "Error: Invalid option\n");
        return 1;
    }

    return 0;
}

void listFiles(const char *diskname) {
    
    struct DirectoryEntry *dirPtr;
    int fd;
    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }
    unsigned char buffer [CLUSTERSIZE];
    int readsuccess = readcluster(fd,buffer,2);
    dirPtr = (struct DirectoryEntry *) buffer;
    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if ( isValidEntry(dirPtr) )
            {
                char name[9];
                strncpy(name, (char*)dirPtr->name, 8);
                name[8] = '\0';

                char ext[4];
                strncpy(ext, (char*)dirPtr->extention, 3);
                ext[3] = '\0';

                // Combine name and ext to form fullName
                char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters
                strcpy(fullName, trim(name));

                if(strlen(trim(ext))){
                    strcat(fullName, ".");
                    strcat(fullName, trim(ext));
                }

        
                printf("FileName: %s\t Extension: %s\t Size: %d\t First cluster: %d\n", trim(name) , trim(ext) , (u_int32_t) dirPtr->fileSize, getFirstCluster(dirPtr));       
            }
        dirPtr++;        
    }

    close(fd);
}


void readFileAscii(const char *diskname, const char *filename) {
    unsigned char cluster[CLUSTERSIZE];
    struct DirectoryEntry *directoryEntry;
    int fd;
    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }
    printf("File read ascii.\n");
    int n = readcluster(fd, cluster, 2);
    directoryEntry = (struct DirectoryEntry *)cluster;
    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if ( isValidEntry (directoryEntry) )
            {

                char name[9];
                strncpy(name, (char*)directoryEntry->name, 8);
                name[8] = '\0';

                char ext[4];
                strncpy(ext, (char*)directoryEntry->extention, 3);
                ext[3] = '\0';

                // Combine name and ext to form fullName
                char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters
                strcpy(fullName, trim(name));

                if(strlen(trim(ext))){
                    strcat(fullName, ".");
                    strcat(fullName, trim(ext));
                }
     

                if (strcasecmp(trim(fullName), trim(filename)) == 0) {
                    printf("Filename match\n");
                    u_int32_t first_cluster = getFirstCluster(directoryEntry);
                    printf("First cluster is %d.\n", first_cluster);
                    if (first_cluster == 0) {
                        printf("File is empty.\n");
                    } 
                    else {
                        // Read file content from clusters
                        unsigned char content[CLUSTERSIZE+1];
                        u_int32_t current_cluster = first_cluster;
                        ssize_t read_bytes;

                        while (1) {
                            read_bytes = readcluster(fd, content, current_cluster);
                            if (read_bytes < 0) {
                                fprintf(stderr, "Error reading file content\n");
                                exit(1);
                            }
                            content[CLUSTERSIZE] = '\0'; // Null-terminate for ASCII output now doesnt overwrite last char.
                            
                            printf("Next cluster is %d\n", getNextCluster(fd, current_cluster));
                            printf("Content of cluster %d: \n%s\n", current_cluster, content);
                            current_cluster = getNextCluster(fd, current_cluster); // Move to the next cluster in the chain
                            if(current_cluster >= 0x0FFFFFF8) //end of file
                            {    
                                 printf("End of file reached\n");
                                return;
                            }
                            
                        }
                    }
                    close(fd);
                    return;

                }
                      
            }
        directoryEntry++;        
    }

    printf("File not found.\n");
    close(fd);
}


void readFileBinary(const char *diskname, const char *filename) {
    unsigned char cluster[CLUSTERSIZE];
    struct DirectoryEntry *directoryEntry;
    int fd;
    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }
    printf("File read binary.\n");
    int n = readcluster(fd, cluster, 2);
    directoryEntry = (struct DirectoryEntry *)cluster;
    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if (isValidEntry (directoryEntry)) {


            char name[9];
            strncpy(name, (char*)directoryEntry->name, 8);
            name[8] = '\0';

            char ext[4];
            strncpy(ext, (char*)directoryEntry->extention, 3);
            ext[3] = '\0';

            // Combine name and ext to form fullName
            char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters
            strcpy(fullName, trim(name));

            if(strlen(trim(ext))){
                strcat(fullName, ".");
                strcat(fullName, trim(ext));
            }
     

            if (strcasecmp(trim(fullName), trim(filename)) == 0) {
                printf("Filename match\n");
                u_int32_t first_cluster = getFirstCluster(directoryEntry);
                printf("First cluster is %d.\n", first_cluster);
                if (first_cluster == 0) {
                    printf("File is empty.\n");
                } else {
                    // Read file content from clusters
                    u_int32_t current_cluster = first_cluster;
                    ssize_t read_bytes;
                    int offset = 0;

                    while (1) {
                        unsigned char content[CLUSTERSIZE];
                        read_bytes = readcluster(fd, content, current_cluster);
                        if (read_bytes < 0) {
                            fprintf(stderr, "Error reading file content\n");
                            exit(1);
                        }
                        printf("Cluster %d content:\n", current_cluster);
                        for (int j = 0; j < CLUSTERSIZE; j++) {
                            if(j + offset >= directoryEntry->fileSize){
                                printf("\nWrite only shows cluster up to filesize.\n");
                                break;
                            }
                                
                            if (j % 16 == 0) {
                                printf("%08x: ", offset + j);
                            }
                            printf("%02x ", content[j]);
                            if ((j + 1) % 16 == 0) {
                                printf("\n");
                            }
                        }
                        printf("\n");

                        current_cluster = getNextCluster(fd, current_cluster); // Move to the next cluster in the chain
                        if (current_cluster >= 0x0FFFFFF8) // end of file
                        {
                            printf("End of file reached\n");
                            break;
                        }

                        offset += CLUSTERSIZE;
                    }
                }
                close(fd);
                return;
            }
        }
        directoryEntry++;
    }

    printf("File not found.\n");
    close(fd);
}


void createFile(const char *diskname, const char *filename) {
    int fd = open(diskname, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }

    unsigned char rootCluster[CLUSTERSIZE];
    readcluster(fd, rootCluster, 2);  // Assuming root directory starts at cluster 2

    struct DirectoryEntry *directoryEntry = (struct DirectoryEntry *)rootCluster;
    int emptyEntryIndex = -1;

    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if ( isValidEntry (directoryEntry))
            {
                char name[9];
                strncpy(name, (char*)directoryEntry->name, 8);
                name[8] = '\0';

                char ext[4];
                strncpy(ext, (char*)directoryEntry->extention, 3);
                ext[3] = '\0';

                // Combine name and ext to form fullName
                char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters
                strcpy(fullName, trim(name));

                if(strlen(trim(ext))){
                    strcat(fullName, ".");
                    strcat(fullName, trim(ext));
                }
     

                if (strcasecmp(trim(fullName), trim(filename)) == 0) {
                    fprintf(stderr, "Error: filename exists in the directory\n");
                    close(fd);
                    exit(1);
                }
                      
            }
        directoryEntry++;        
    }

    directoryEntry = (struct DirectoryEntry *)rootCluster;

    // Find an empty directory entry
    for (int i = 0; i < 32; i++) {
        if (!isValidEntry (directoryEntry)) {
            emptyEntryIndex = i;
            break;
        }
        directoryEntry++;
    }

    if (emptyEntryIndex == -1) {
        fprintf(stderr, "Error: No empty directory entry available\n");
        close(fd);
        exit(1);
    }

    char name[8];
    char ext[3];

    parseFilename(filename, name, ext);

    strncpy((char *)directoryEntry->name, name, 8);
    strncpy((char *)directoryEntry->extention, ext, 3);  // Assuming no extension
    directoryEntry->attribute[1] = 0x00;  // Normal file attribute
    directoryEntry->fileSize = 0;

    u_int32_t firstCluster = findFirstEmptyCluster(fd);
    if (firstCluster == 0) {
        fprintf(stderr, "Error: Failed to allocate clusters\n");
        close(fd);
        exit(1);
    }

    
    u_int16_t high = (u_int16_t)((firstCluster >> 16) & 0xFFFF);
    u_int16_t low = (u_int16_t)(firstCluster & 0xFFFF);

    // Store high and low bytes
    directoryEntry->firstclusterhigh = high;
    directoryEntry->firstclusterlow = low;


    u_int32_t table [FAT_SIZE];
   
	lseek (fd, 512 * 32, SEEK_SET);
	int n  = read (fd, table, 1020*512);
	if (n == FAT_SIZE){
        table[firstCluster] = 0x0FFFFFFF;
    }

    lseek (fd, 512 * 32, SEEK_SET);
	n  = write (fd, table, 1020*512);
    fsync (fd);
    if (n == 1020*512)
	    printf("Sucessfully fat table updated.\n");  



    // Write the updated root directory cluster back to the disk
    writecluster(fd, rootCluster, 2);

    close(fd);
}

void deleteFile(const char *diskname, const char *filename) {

    unsigned char cluster[CLUSTERSIZE];
    struct DirectoryEntry *directoryEntry;
    int fd;
    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }
    printf("File delete called.\n");
    int n = readcluster(fd, cluster, 2);
    directoryEntry = (struct DirectoryEntry *)cluster;
    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if ( directoryEntry->name[0])
            {
                char name[9];
                strncpy(name, (char*)directoryEntry->name, 8);
                name[8] = '\0';

                char ext[4];
                strncpy(ext, (char*)directoryEntry->extention, 3);
                ext[3] = '\0';

                // Combine name and ext to form fullName
                char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters
                strcpy(fullName, trim(name));

                if(strlen(trim(ext))){
                    strcat(fullName, ".");
                    strcat(fullName, trim(ext));
                }
     

                if (strcasecmp(trim(fullName), trim(filename)) == 0) {
                    printf("File to delete found\n");


                    //Fat table accessed
                    u_int32_t table [FAT_SIZE];
                
                    lseek (fd, 512 * 32, SEEK_SET);
                    int n  = read (fd, table, 1020*512);
                    if (n != FAT_SIZE){
                        printf("Fat table error at delete\n");
                    }

                    u_int32_t first_cluster = getFirstCluster(directoryEntry);
                    printf("First cluster is %d.\n", first_cluster);

                    memset(directoryEntry->name, 0, sizeof(directoryEntry->name));
                    memset(directoryEntry->extention, 0, sizeof(directoryEntry->extention));
                    memset(directoryEntry->attribute, 0, sizeof(directoryEntry->attribute));
                    memset(directoryEntry->mscreation, 0, sizeof(directoryEntry->mscreation));
                    memset(directoryEntry->datecreation, 0, sizeof(directoryEntry->datecreation));
                    memset(directoryEntry->lastAccessDate, 0, sizeof(directoryEntry->lastAccessDate));
                    //memset(directoryEntry->firstclusterhigh, 0, sizeof(directoryEntry->firstclusterhigh));
                    memset(directoryEntry->lastWriteDate, 0, sizeof(directoryEntry->lastWriteDate));
                    //memset(directoryEntry->firstclusterlow, 0, sizeof(directoryEntry->firstclusterlow));
                    directoryEntry->fileSize = 0;
                    directoryEntry->firstclusterhigh = 0;
                    directoryEntry->firstclusterlow = 0;
                    writecluster(fd, cluster, 2);

                    if (first_cluster <= 2) {
                        printf("File first cluster is <= 2.\n");
                    }
                    else {
                        // Read file content from clusters
                        unsigned char content[CLUSTERSIZE];
                        for (size_t i = 0; i < CLUSTERSIZE; i++)
                        {
                            content[i] = 0x00;
                        }
                        
                        u_int32_t current_cluster = first_cluster;
                        ssize_t read_bytes;

                        while (1) {
                            read_bytes = writecluster(fd, content, current_cluster);
                            if (read_bytes < 0) {
                                fprintf(stderr, "Error writing file content\n");
                                exit(1);
                            }
                            printf("Current cluster is %d\n", current_cluster);
                            printf("Next cluster is %d\n", getNextCluster(fd, current_cluster));
                            u_int32_t nextcluster;
                            nextcluster = getNextCluster(fd, current_cluster); // Move to the next cluster in the chain
                            table[current_cluster] = 0;
                            lseek (fd, 512 * 32, SEEK_SET);
                            n  = write (fd, table, 1020*512);
                            fsync (fd);
                            if (n == 1020*512)
                                printf("Fat table successfully updated.\n");  
                                
                            printf("Now cluster %d is emptied\n", current_cluster);
                            if(nextcluster >= 0x0FFFFFF8) //end of file
                            {    
                                printf("End of file reached\n");
                                read_bytes = writecluster(fd, content, current_cluster);
                                if (read_bytes < 0) {
                                    fprintf(stderr, "Error writing file content\n");
                                    exit(1);
                                }
                                return;
                            }

                            current_cluster = nextcluster;
                            
                        }
                    }
                    //update fat table
                    lseek (fd, 512 * 32, SEEK_SET);
                    n  = write (fd, table, 1020*512);
                    fsync (fd);
                    if (n == 1020*512)
                        printf("Sucessfully fat table updated.\n");  
                    close(fd);
                    return;

                }
                      
            }
        directoryEntry++;        
    }

    printf("File not found.\n");
    close(fd);
}

void writeFile(const char *diskname, const char *filename, unsigned int offset, unsigned int n, unsigned char data) {
    unsigned int byteslefttowrite = n;
    unsigned int curroffset = offset;
    unsigned char cluster[CLUSTERSIZE];
    u_int32_t newfileSize;
    u_int32_t oldfileSize;
    struct DirectoryEntry *directoryEntry;
    int fd;
    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open disk image\n");
        exit(1);
    }
    printf("File write called.\n");
    int q = readcluster(fd, cluster, 2);
    directoryEntry = (struct DirectoryEntry *)cluster;
    for (size_t i = 0; i < 32; i++) // 32 directory entries in 1 cluster
    {
        if ( isValidEntry (directoryEntry) )
            {
                char name[9];
                strncpy(name, (char*)directoryEntry->name, 8);
                name[8] = '\0';

                char ext[4];
                strncpy(ext, (char*)directoryEntry->extention, 3);
                ext[3] = '\0';

                // Combine name and ext to form fullName
                char fullName[13]; // Assuming maximum file name length is 8 characters and extension is 3 characters 1 dot and 1 null char
                strcpy(fullName, trim(name));

                if(strlen(trim(ext))){
                    strcat(fullName, ".");
                    strcat(fullName, trim(ext));
                }
     

                if (strcasecmp(trim(fullName), trim(filename)) == 0) {
                    printf("File to write found\n");
                    oldfileSize = directoryEntry->fileSize;
                    newfileSize = oldfileSize;
                    if(offset > oldfileSize){

                        printf("offset > fileSize\n");
                        return;
                    }
                    if(n + offset > oldfileSize)
                        newfileSize = n + offset;

                    //Fat table accessed
                    u_int32_t table [FAT_SIZE];
                
                    lseek (fd, 512 * 32, SEEK_SET);
                    int n  = read (fd, table, 1020*512);
                    if (n != FAT_SIZE){
                        printf("Fat table error at write\n");
                    }
                    

                    u_int32_t first_cluster = getFirstCluster(directoryEntry);
                    printf("First cluster is %d.\n", first_cluster);

                    writecluster(fd, cluster, 2);

                    if (first_cluster <= 2) {
                        printf("File first cluster is <= 2.\n");
                        return;
                    }
                    else {
                        // Read file content from clusters
                       
                        
                        u_int32_t current_cluster = first_cluster;


                        for (unsigned int k = 0; k < curroffset / CLUSTERSIZE;) {
                            current_cluster = getNextCluster(fd, current_cluster);
                            curroffset = curroffset - CLUSTERSIZE;
                            if (current_cluster >= 0x0FFFFFF8) {
                                printf("Error: Offset exceeds file size.\n");
                                close(fd);
                                exit(1);
                            }
                        }
                        ssize_t read_bytes;

                        while (1) {

                            int fullywritten = FALSE;
                            lseek (fd, 512 * 32, SEEK_SET);
                            int n  = read (fd, table, 1020*512);
                            if (n != FAT_SIZE){
                                printf("Fat table error at write\n");
                            }
                            //FAT TABLE INFO RECIEVED
                            unsigned char content[CLUSTERSIZE];

                            readcluster(fd, content, current_cluster);

                            for (int i = curroffset; i < CLUSTERSIZE; i++)
                            {
                                curroffset = 0;
                                content[i] = data;
                                byteslefttowrite--;
                                if(byteslefttowrite == 0){
                                    fullywritten = TRUE;
                                    break;
                                }
                            }

                            read_bytes = writecluster(fd, content, current_cluster);
                            if (read_bytes < 0) {
                                fprintf(stderr, "Error writing file content\n");
                                exit(1);
                            }

                            printf("Current cluster is %d\n", current_cluster);
                            printf("Next cluster is %d\n", getNextCluster(fd, current_cluster));
                            u_int32_t nextcluster;
                            nextcluster = getNextCluster(fd, current_cluster); // Move to the next cluster in the chain
                            if(nextcluster >= 0x0FFFFFF8) //last file need more clusters
                            {    
                                if(fullywritten){ //end of file but dont need more files
                                    printf("File fully written. \n"); //case 1
                                }
                                else{ //end of file and need more files

                                    printf("End of file reached but more clusters needed.\n"); //case2

                                    nextcluster = findFirstEmptyCluster(fd);
                                    table[current_cluster] = nextcluster;
                                    table[nextcluster] = 0xFFFFFFFF;
                                    lseek (fd, 512 * 32, SEEK_SET);
                                    n  = write (fd, table, 1020*512);
                                    fsync (fd);

                                    if (n == 1020*512)
                                        printf("Sucessfully updated fat table for write operation.\n");  

                                }
                                
                            }
                            else // not end of file
                            {    
                                printf("End of file reached continue case3.\n");
                                
                            }

                            current_cluster = nextcluster;

                            if(fullywritten){
                                directoryEntry->fileSize = newfileSize;
                                writecluster(fd, cluster, 2);
                                printf("Everything successful, finishing writing.\n");
                                return;

                            }
                            
                        }
                    }
                    //update fat table
                    lseek (fd, 512 * 32, SEEK_SET);
                    n  = write (fd, table, 1020*512);
                    fsync (fd);
                    if (n == 1020*512)
                        printf("Sucessfully fat table updated.\n");  
                    close(fd);
                    return;

                }
                      
            }
        directoryEntry++;        
    }


    close(fd);
}

void printHelp() {
    printf("Usage: fatmod DISKIMAGE OPTION [PARAMETERS]\n");
    printf("Options:\n");
    printf("  -l\t\t\tList files in the root directory.\n");
    printf("  -r -a FILENAME\tRead FILENAME in ASCII format.\n");
    printf("  -r -b FILENAME\tRead FILENAME in binary format. Shows all bytes in clusters up to filesize.\n");
    printf("  -c FILENAME\t\tCreate a file named FILENAME. No duplicate names.\n");
    printf("  -d FILENAME\t\tDelete the file named FILENAME.\n");
    printf("  -w FILENAME OFFSET N DATA\n     Write DATA into FILENAME starting at OFFSET for N bytes OFFSET should not be bigger than file size\n");
    
    printf("  -h\t\t\tDisplay this help page\n");
    printf("     All filenames are case insensitive meaning file1.txt is equal to FILE1.TXT in all operations\n");
    printf("     Reading files with binary format does not show empty bytes (bytes after the file size).\n");
}