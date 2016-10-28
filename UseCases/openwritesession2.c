#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/ipc.h>
#include <errno.h>

#define SESSION_OPEN 00000004

int main(int argc, char** argv){
        int mode,flags,ret,fd;
        const char* filename;
        const char buffer[10];
        const char* new_content;
        if(argc>3){
                filename = argv[1];
                new_content=argv[2];
                flags=strtol(argv[3],NULL,10);
                if(!((flags&O_RDONLY)||(flags&O_WRONLY)||(flags&O_RDWR))) {
                        printf("Invalid arguments: at least provide one out O_RDONLY,O_WRONLY and O_RDWR as flag\n");
                        return EINVAL;
                }
                if((flags&O_CREAT)&&argc==4) {
                        printf("Invalid arguments: if flag O_CREAT is given, mode has to be specified as third argument\n");
                        return EINVAL;
                }
                mode=0;
                if(argc==5)
                        mode = strtol(argv[4],NULL,10);
                printf("PID of current process:%d\n",getpid());
                printf("Opening file using session semantics %s with flags %d and mode %d\n",filename,flags,mode);
                fd=open(filename,flags|SESSION_OPEN,mode);
                if(fd<0) {
                        switch(errno){
                                case ENOMEM: {
                                        printf("Error while opening session:not enough memory available\n");
                                        break;
                                }
                                case EINVAL:{
                                        printf("Error while opening session: bad parameters\n");
                                        break;
                                }
                                case EFAULT:{
                                        printf("Error while opening session: bad address\n");
                                        break;
                                }
                                case EIO:{
                                        printf("Error while opening session: could not transfer file page\n");
                                        break;
                                }
                                default:
                                        printf("Error while opening session:%d\n",errno);
                        }
                        return errno;
                }
                printf("File descriptor:%d\n",fd);
                printf("Reading \"%s\"\n",filename);
                ret=read(fd,buffer,10);
                if(!ret){
                        printf("Could not read file because of EOF or empty file\n");
                        return EOF;
                }
                if(ret<0) {
                        switch(errno){
                                case EIO:{
                                        printf("Error while reading session: could not copy some bytes\n");
                                        break;
                                }
                                case EINVAL:{
                                        printf("Error while going to sleep on barrier: invalid barrier id or tag\n");
                                        break;
                                }
                                default:
                                        printf("Could not read file because of error:%d\n",errno);
                        }
                        return errno;
                }
                printf("Bytes read:%d\n",ret);
                printf("Content read:%s\n",buffer);
                printf("Writing new content...\n");
                ret=write(fd,new_content,strlen(new_content));
                if(ret>=0)
                        printf("%d bytes written into original file\n",ret);
                else
                        printf("Could not write into session because of error:%d\n",ret);
                sleep(5);
                printf("Now closing session\n");
                return  ret;
        }
        printf("Invalid arguments: at least provide absolute filepath as first parameter, content to write as second one and flag as third one;\n"
                       "optionally provide mode as fourth parameter\n");
}
