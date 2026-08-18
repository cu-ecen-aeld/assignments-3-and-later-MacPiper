#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    char *fileDir, *fileStr;

    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        fprintf(stderr,"Please call write with two arguments: writer <fileDir> <fileStr>\n");
        syslog(LOG_ERR, "Tool was called with %d instead of exactly 2 arguments", (argc-1));
        return 1;
    } else {
        fileDir = argv[1];
        fileStr = argv[2];
    }

    printf("Create file %s with contents %s\n", fileDir, fileStr);
    syslog(LOG_DEBUG,"Writing %s to %s",fileStr, fileDir);
    FILE *file = fopen(fileDir,"w");
    if (file == NULL) {
        fprintf(stderr, "Was not able to open %s for writing.\nValue of errno is %d\n",fileDir, errno);
        perror("perror tells");
        fprintf(stderr,"strerror(errno) tells : %s\n", strerror(errno));
        syslog(LOG_ERR, "Tool was not able to open %s for writing", fileDir);
        return 1;
    } else {
        printf("File %s opened successfully\n", fileDir);
    }

    fprintf(file,"%s",fileStr);
    fclose(file);
}
