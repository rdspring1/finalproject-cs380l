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

#define ARGSNUM 3
#define BTSIZE 10
#define ERROR -1
#define NOPENFD 50
#define AIOMAX 1

struct fileaio
{
	int srcfd;
	int destfd;
	off_t filesize;
	struct aiocb** list;
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
		printf("syncpy: %s\n", msg);
	}
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

void write_aio_handler(sigval_t sigval)
{
	struct aiocb** list = (struct aiocb**) sigval.sival_ptr;
	// Handle Write AIO
	for(int pos = 0; pos < AIOMAX && list[pos]; ++pos)
	{
		if(aio_error(list[pos]))
		{
			strexit("WRITE AIO ERROR");
		}
	}	
	--outstanding_aio;
	//sem_post(&sema_aio);
}

void read_aio_handler(sigval_t sigval)
{
	const unsigned PAGESIZE = sysconf(_SC_PAGESIZE);
	struct fileaio* arg = (struct fileaio*) sigval.sival_ptr;
	struct aiocb* writelist[AIOMAX];

	// Handle Write AIO
	int pos;
	for(pos = 0; pos < AIOMAX && arg->list[pos]; ++pos)
	{
		if(aio_error(arg->list[pos]))
		{
			strexit("READ AIO ERROR");
		}

		writelist[pos] = (struct aiocb*) malloc(sizeof(struct aiocb));
		if(!writelist[pos])
		{
			strexit("AIOCB MALLOC");
		}

		writelist[pos]->aio_fildes = arg->destfd;
		writelist[pos]->aio_buf = arg->list[pos]->aio_buf;
		writelist[pos]->aio_nbytes = PAGESIZE;
		writelist[pos]->aio_offset = arg->list[pos]->aio_offset;
		writelist[pos]->aio_lio_opcode = LIO_WRITE;
	}

	// Setup Sigevent
	struct sigevent write_sig_evnt;
	write_sig_evnt.sigev_notify = SIGEV_THREAD;
	write_sig_evnt.sigev_notify_function = write_aio_handler;
	write_sig_evnt.sigev_notify_attributes = NULL;
	write_sig_evnt.sigev_value.sival_ptr = &writelist;
	if(lio_listio(LIO_NOWAIT, writelist, AIOMAX, &write_sig_evnt) == ERROR)
	{
		strexit("WRITE LIO_LISTIO");
	}
	++outstanding_aio;

	if(arg->list[pos-1]->aio_offset + PAGESIZE < arg->filesize)
	{
		struct aiocb* readlist[AIOMAX];
		for(unsigned i = 0, offset = arg->list[pos-1]->aio_offset + PAGESIZE; i < AIOMAX && offset < arg->filesize; ++i, offset += PAGESIZE)
		{
			readlist[i] = (struct aiocb*) malloc(sizeof(struct aiocb));
			if(!readlist[i])
			{
				strexit("AIOCB MALLOC");
			}

			readlist[i]->aio_fildes = arg->srcfd;
			readlist[i]->aio_buf = malloc(PAGESIZE);
			if(!readlist[i]->aio_buf)
			{
				strexit("AIOCB BUFFER");
			}
			readlist[i]->aio_nbytes = PAGESIZE;
			readlist[i]->aio_offset = offset;
			readlist[i]->aio_lio_opcode = LIO_READ;
		}

		arg->list = readlist;

		// Setup Sigevent
		struct sigevent read_sig_evnt;
		read_sig_evnt.sigev_notify = SIGEV_THREAD;
		read_sig_evnt.sigev_notify_function = read_aio_handler;
		read_sig_evnt.sigev_notify_attributes = NULL;
		read_sig_evnt.sigev_value.sival_ptr = arg;
		if(lio_listio(LIO_NOWAIT, readlist, AIOMAX, &read_sig_evnt) == ERROR)
		{
			strexit("READ LIO_LISTIO");
		}
		++outstanding_aio;
		printf("Outstanding_AIO: %d\n", outstanding_aio);
	}
	--outstanding_aio;
	sem_post(&sema_aio);
}


int copy_file(const char *srcpath, const char *destpath)
{
	const unsigned PAGESIZE = sysconf(_SC_PAGESIZE);
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
		if((destfd = open(destpath, O_WRONLY | O_CREAT, st->st_mode)) == ERROR)
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

	if(st->st_size > 0 && fallocate(destfd, 0, 0, st->st_size) == ERROR)
	{
		strexit("FALLOCATE");
	}

	struct aiocb* list[AIOMAX];
	for(unsigned i = 0, offset = 0; i < AIOMAX && offset < st->st_size; ++i, offset += PAGESIZE)
	{
		list[i] = (struct aiocb*)malloc(sizeof(struct aiocb));
		if(!list[i])
		{
			strexit("AIOCB MALLOC");
		}

		list[i]->aio_fildes = srcfd;
		list[i]->aio_buf = malloc(PAGESIZE);
		if(!list[i]->aio_buf)
		{
			strexit("AIOCB BUFFER");
		}
		list[i]->aio_nbytes = PAGESIZE;
		list[i]->aio_offset = offset;
		list[i]->aio_lio_opcode = LIO_READ;
	}

	struct fileaio arg;
	arg.srcfd = srcfd;
	arg.destfd = destfd;
	arg.filesize = st->st_size;
	arg.list = list;

	// Setup Sigevent
	struct sigevent sig_evnt;
	sig_evnt.sigev_notify = SIGEV_THREAD;
	sig_evnt.sigev_notify_function = read_aio_handler;
	sig_evnt.sigev_notify_attributes = NULL;
	sig_evnt.sigev_value.sival_ptr = &arg;
	if(lio_listio(LIO_NOWAIT, list, AIOMAX, &sig_evnt) == ERROR)
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
		//sem_wait(&sema_aio);
	}
	sem_destroy(&sema_aio);

	cleanup();
	exit(EXIT_SUCCESS);
}

