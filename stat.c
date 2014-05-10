#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include "job.h"

/* 
 * 命令语法格式
 *     stat
 */
void usage()
{
	printf("Usage: stat\n");		
}

int main(int argc,char *argv[])
{
	struct jobcmd statcmd;
	int fd, fifo;
	char fifobuf[FIFOLEN];

	if(argc!=1)
	{
		usage();
		return 1;
	}

	statcmd.type=STAT;
	statcmd.defpri=0;
	statcmd.owner=getuid();
	statcmd.argnum=0;

	#ifdef DEBUG
		printf("statcmd type\t%d (-1 means ENQ, -2 meas DEQ, -3 means STAT)\n"
						"statcmd owner\t%d\n",
						statcmd.type, statcmd.owner);
	#endif
	if((fd=open("/tmp/server",O_WRONLY))<0)
		error_sys("stat open fifo failed");

	if(write(fd,&statcmd,DATALEN)<0)
		error_sys("stat write failed");

	if((fifo=open("/tmp/stat", O_RDONLY))<0)
		error_sys("stat read fifo failed");

	if(read(fifo, fifobuf, FIFOLEN)<0)
		error_sys("stat write(back) failed");
	printf("%s\n", fifobuf);
	close(fd);
	return 0;
}
