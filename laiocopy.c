#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <aio.h>
#include <signal.h>
#include <semaphore.h>
#include <algorithm>

#define BUFSIZE 1
#define ARGSNUM 3
#define BTSIZE 10
#define ERROR -1
#define NOPENFD 50
#define AIOSUSPEND 1

struct fileaio
{
	int srcfd;
	int destfd;
	off_t filesize;
	off_t offset;
	struct aiocb* aio;
};

std::string* srcfilepath;
std::string* destfilepath;
int outstanding_aio = 0;
sem_t sema_aio;


void cleanup()
{
	free(destfilepath);
}

void print_backtrace()
{
	printf("Backtrace\n");
	void* btlist[BTSIZE];
	int numaddr = backtrace(btlist, BTSIZE);
	char** strings = backtrace_symbols(btlist, numaddr);
	if(strings)
	{
		for(int n = 0; n < numaddr; ++n)
		{
			printf("%s\n", strings[n]);
		}
	}
	printf("\n");
}

void strexit()
{
	print_backtrace();
	printf("Error: %s\n", strerror(errno));
	cleanup();
	exit(EXIT_FAILURE);
}

void strexit(const char* msg)
{
	print_backtrace();
	if(errno)
	{
		printf("Error: %s: %s\n", msg, strerror(errno));
	}
	else
	{
		printf("laiocpy: %s\n", msg);
	}
	cleanup();
	exit(EXIT_FAILURE);
}

void strexit(const char* msg, int errval)
{
	print_backtrace();
	printf("Error: %s: %s\n", msg, strerror(errval));
	cleanup();
	exit(EXIT_FAILURE);
}

bool samefile(int fd1, int fd2)
{
	struct stat stat1, stat2;
	if(fstat(fd1, &stat1) == ERROR) strexit("FSTAT");
	if(fstat(fd2, &stat2) == ERROR) strexit("FSTAT");
	return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}

std::string filepathname(const std::string& str, int level)
{
	size_t pos = str.length();
	if(str.find_last_of("/\\", pos-1) == str.length()-1)
	{
		--pos;
	}		

	for(int i = 0; i < level; ++i)
	{
		pos = str.find_last_of("/\\", pos-1);
	}
	return str.substr(pos);
}

void aio_handler(sigval_t sigval)
{
	const unsigned PAGESIZE = sysconf(_SC_PAGESIZE) * BUFSIZE;
	struct fileaio* arg = (struct fileaio*) sigval.sival_ptr;
	void* buffer = malloc(PAGESIZE);
	void* aiobuffer = NULL;
	struct aiocb* cblist[AIOSUSPEND] = {};	
	cblist[0] = arg->aio;

	if(aio_error(arg->aio))
	{
		strexit("READ AIO ERROR", aio_return(arg->aio));
	}

	size_t dataread = aio_return(arg->aio);
	aiobuffer = (void*) arg->aio->aio_buf;
	std::swap(buffer, aiobuffer);

	arg->aio->aio_sigevent.sigev_notify = SIGEV_NONE;
	for(off_t offset = arg->offset; offset < arg->filesize; offset += PAGESIZE)
	{
		arg->aio->aio_offset = offset;
		
		if(aio_read(arg->aio) == ERROR)
		{
			strexit("READ LIO_LISTIO");
		}

		if(write(arg->destfd, buffer, dataread) == ERROR)
		{
			strexit("WRITE");
		}

		if(aio_suspend(cblist, AIOSUSPEND, NULL) == ERROR)
		{
			strexit("AIO SUSPEND");
		}
		
		dataread = aio_return(arg->aio);
		aiobuffer = (void*) arg->aio->aio_buf;
		std::swap(buffer, aiobuffer);
	}
	
	if(write(arg->destfd, buffer, dataread) == ERROR)
	{
		strexit("WRITE");
	}

	--outstanding_aio;
	sem_post(&sema_aio);
}


int copy_file(const char *srcpath, const char *destpath)
{
	const unsigned PAGESIZE = sysconf(_SC_PAGESIZE) * BUFSIZE;
	int srcfd = open(srcpath, O_RDONLY);
	if(srcfd == ERROR)
	{
		strexit(srcpath);
	}

	struct stat* st = (struct stat*) malloc(sizeof(struct stat));
	if(fstat(srcfd, st) == ERROR) 
	{ 
		strexit("FSTAT");
	}

	int destfd = ERROR;
	if(S_ISDIR(st->st_mode))
	{
		if(mkdir(destpath, st->st_mode) == ERROR)
		{
			strexit(destpath);
		}
		return 0;
	}
	else
	{
		if((destfd = open(destpath, O_WRONLY | O_CREAT | O_APPEND, st->st_mode)) == ERROR)
		{
			strexit(destpath);
		}
	}

	if(samefile(srcfd, destfd))
	{
		return true;
	}

	if(ftruncate(destfd, 0) == ERROR)
	{
		strexit("TRUNCATE");
	}

	if(st->st_size > 0 && fallocate(destfd, FALLOC_FL_KEEP_SIZE, 0, st->st_size) == ERROR)
	{
		strexit("FALLOCATE");
	}

	// AIO Read / Write Information
	struct fileaio* arg = (struct fileaio*) malloc(sizeof(struct fileaio));
	if(!arg)
	{
		strexit("FILEAIO MALLOC");
	}
	arg->srcfd = srcfd;
	arg->destfd = destfd;
	arg->filesize = st->st_size;
	arg->offset = PAGESIZE;

	arg->aio = (struct aiocb*) calloc(1, sizeof(struct aiocb));
	if(!arg->aio)
	{
		strexit("AIOCB CALLOC");
	}

	arg->aio->aio_fildes = srcfd;
	arg->aio->aio_buf = malloc(PAGESIZE);
	if(!arg->aio->aio_buf)
	{
		strexit("AIOCB BUFFER");
	}
	arg->aio->aio_nbytes = PAGESIZE;
	arg->aio->aio_offset = 0;

	// Setup sigevent handler for when AIO finishes
	arg->aio->aio_sigevent.sigev_notify = SIGEV_THREAD;
	arg->aio->aio_sigevent.sigev_notify_function = aio_handler;
	arg->aio->aio_sigevent.sigev_notify_attributes = NULL;
	arg->aio->aio_sigevent.sigev_value.sival_ptr = arg;

	if(aio_read(arg->aio) == ERROR)
	{
		strexit("READ LIO_LISTIO");
	}
	++outstanding_aio;

	return 0;
}

static int copy_directory(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
	//printf("%-3s %2d %7jd   %-40s %d %s\n",
	//(tflag == FTW_D) ?   "d"   : (tflag == FTW_DNR) ? "dnr" :
	//(tflag == FTW_DP) ?  "dp"  : (tflag == FTW_F) ?   "f" :
	//(tflag == FTW_NS) ?  "ns"  : (tflag == FTW_SL) ?  "sl" :
	//(tflag == FTW_SLN) ? "sln" : "???",
	//ftwbuf->level, (intmax_t) sb->st_size,
	//fpath, ftwbuf->base, fpath + ftwbuf->base);
	if(ftwbuf->level == 0)
	{
		return 0;
	}

	copy_file(fpath, (*destfilepath + filepathname(std::string(fpath), ftwbuf->level)).c_str());	
	return 0;           /* To tell nftw() to continue */
}

int main(int argc, char *argv[])
{
	if(argc != ARGSNUM)	
	{
		strexit("Missing Operands");
	}	

	{
		char* pos = strchr(argv[1], '/');
		if(!pos || ((size_t) (pos-argv[1]+1) == strlen(argv[1])))
		{		
			srcfilepath = new std::string("./");
			srcfilepath->append(argv[1]);
		}
		else
		{
			srcfilepath = new std::string(argv[1]);
		}
	}

	{
		char* pos = strchr(argv[2], '/');
		if(!pos || ((size_t) (pos-argv[2]+1) == strlen(argv[2])))
		{		
			destfilepath = new std::string("./");
			destfilepath->append(argv[2]);
		}
		else
		{
			destfilepath = new std::string(argv[2]);
		}
	}

	// Initialize Semaphore to track asynchronous I/O
	sem_init(&sema_aio, 0, 0);

	struct stat* srcst = (struct stat*) malloc(sizeof(struct stat));
	if(stat(srcfilepath->c_str(), srcst) == ERROR) 
	{ 
		strexit("SRC FSTAT");
	}

	struct stat* destst = (struct stat*) malloc(sizeof(struct stat));
	if(stat(destfilepath->c_str(), destst) == ERROR) 
	{ 
		if(errno != ENOENT)
		{
			strexit("DEST FSTAT");
		}
		else 
		{
			// create file or directory depending on srcfile
			if(S_ISDIR(srcst->st_mode))
			{
				if(mkdir(destfilepath->c_str(), S_IRWXU | S_IRWXG) == ERROR)
				{
					strexit("MKDIR");
				}
				// copy_directory
				if (nftw(srcfilepath->c_str(), copy_directory, NOPENFD, FTW_PHYS) == ERROR) 
				{
					strexit("FTW");
				}
			}
			else
			{
				copy_file(srcfilepath->c_str(), destfilepath->c_str());
			}
		}
	}
	else
	{
		if(!S_ISDIR(srcst->st_mode)) 
		{
			if(!S_ISDIR(destst->st_mode)) // SRC-FILE DEST-FILE
			{
				copy_file(srcfilepath->c_str(), destfilepath->c_str());
			}
			else // SRC-FILE DEST-DIRECTORY
			{
				copy_file(srcfilepath->c_str(), (destfilepath->append(filepathname(*srcfilepath, 1))).c_str());
			}
		}
		else if(S_ISDIR(destst->st_mode)) // SRC-DIRECTORY AND DEST-DIRECTORY
		{
			destfilepath->append(filepathname(*srcfilepath, 1));	
			copy_file(srcfilepath->c_str(), destfilepath->c_str());
			// copy_directory
			if (nftw(srcfilepath->c_str(), copy_directory, NOPENFD, FTW_PHYS) == ERROR) 
			{
				strexit("FTW");
			}
		}
		else
		{
			strexit("Cannot copy directory to file");
		}
	}

	// WAIT FOR ASYNCHRONOUS I/O TO FINISH
	while(outstanding_aio > 0)
	{
		sem_wait(&sema_aio);
	}
	sem_destroy(&sema_aio);

	cleanup();
	exit(EXIT_SUCCESS);
}

