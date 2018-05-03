#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

void sampleKernelVersion(char *buffer);
void sampleOsRelease(char *buffer);
void sampleOsType(char *buffer);


void sampleKernelVersion(char *buffer){
    int f=0;
    int rc = 0;
    char *file="/proc/sys/kernel/version";
    f = open(file, O_RDONLY);
    if (f == 0)
    {
        printf("error to open: %s\n", file);
        exit(EXIT_FAILURE);
    }
    rc = read(f, (void *)buffer, 80);
    if (rc < 0){
        printf("read /proc/sys/kernel/version failed!\n");
    }
    buffer[strlen(buffer)-1]=0;                 /* 简单实现tr()函数的功能 */
    close(f);
}
 
void sampleOsRelease(char *buffer){
    int f=0;
    int rc = 0;
    char *file="/proc/sys/kernel/osrelease";
    f = open(file, O_RDONLY);
    if (f == 0)
    {
        printf("error to open: %s\n", file);
        exit(EXIT_FAILURE);
    }
    rc = read(f, (void *)buffer, 80);
    if (rc < 0){
        printf("read /proc/sys/kernel/osrelease failed!\n");
    }
    buffer[strlen(buffer)-1]=0;
    close(f);
}
 
void sampleOsType(char *buffer){
    int f=0;
    int rc = 0;
    char *file="/proc/sys/kernel/ostype";
    f = open(file, O_RDONLY);
    if (f == 0)
    {
        printf("error to open: %s\n", file);
        exit(EXIT_FAILURE);
    }
    rc = read(f, (void *)buffer, 80);
    if (rc < 0){
        printf("read /proc/sys/kernel/ostype failed!\n");
    }
    buffer[strlen(buffer)-1]=0;
    close(f);
}
