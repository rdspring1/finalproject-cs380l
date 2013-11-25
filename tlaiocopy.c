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

#define BUFSIZE 1
#define ARGSNUM 3
#define BTSIZE 10
#define ERROR -1
#define NOPENFD 50
#define AIOMAX 10

struct fileaio
{
	int srcfd;
	int destfd;
	int count;
	off_t filesize;
	off_t offset;
	struct sigevent* handler;
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

void write_aio_handler(sigval_t sigval)
{
	struct fileaio* arg = (struct fileaio*) sigval.sival_ptr;
	// Handle Write AIO
	for(int pos = 0; pos < arg->count; ++pos)
	{
		if(aio_error(arg->list[pos]))
		{
			strexit("WRITE AIO ERROR", aio_return(arg->list[pos]));
		}
	}	
	--outstanding_aio;
	sem_post(&sema_aio);
}

void read_aio_handler(sigval_t sigval)
{
	const unsigned PAGESIZE = sysconf(_SC_PAGESIZE) * BUFSIZE;
	struct fileaio* oldarg = (struct fileaio*) sigval.sival_ptr;

	if(oldarg->offset <= oldarg->filesize)
	{
		struct aiocb** list = new struct aiocb*[AIOMAX]();
		unsigned offset, pos;
		for(pos = 0, offset = oldarg->offset; pos < AIOMAX && offset <= oldarg->filesize; ++pos, offset += PAGESIZE)
		{
			list[pos] = (struct aiocb*)malloc(sizeof(struct aiocb));
			if(!list[pos])
			{
				strexit("AIOCB MALLOC");
			}

			list[pos]->aio_fildes = oldarg->srcfd;
			list[pos]->aio_buf = malloc(PAGESIZE);
			if(!list[pos]->aio_buf)
			{
				strexit("AIOCB BUFFER");
			}
			list[pos]->aio_nbytes = PAGESIZE;
			list[pos]->aio_offset = offset;
			list[pos]->aio_lio_opcode = LIO_READ;
		}

		// Setup sigevent handler for when AIO finishes
		struct sigevent* sig_evnt = (struct sigevent*) malloc(sizeof(struct sigevent));
		sig_evnt->sigev_notify = SIGEV_THREAD;
		sig_evnt->sigev_notify_function = read_aio_handler;
		sig_evnt->sigev_notify_attributes = NULL;

		// AIO Read / Write Information
		struct fileaio* newarg = (struct fileaio*) malloc(sizeof(struct fileaio));
		newarg->srcfd = oldarg->srcfd;
		newarg->destfd = oldarg->destfd;
		newarg->filesize = oldarg->filesize;
		newarg->list = list;
		newarg->offset = offset;
		newarg->count = pos;
		newarg->handler = sig_evnt;

		// Set sigevnt handler argument value
		sig_evnt->sigev_value.sival_ptr = newarg;

		if(lio_listio(LIO_NOWAIT, newarg->list, AIOMAX, newarg->handler) == ERROR)
		{
			strexit("READ LIO_LISTIO");
		}
		++outstanding_aio;
	}

	// Handle Write AIO -> Convert Read AIO to Write AIO
	for(int pos = 0; pos < oldarg->count; ++pos)
	{
		if(aio_error(oldarg->list[pos]))
		{
			strexit("READ AIO ERROR", aio_return(oldarg->list[pos]));
		}

		if(write(oldarg->destfd, (void*) oldarg->list[pos]->aio_buf, aio_return(oldarg->list[pos])) == ERROR)
		{
			strexit("WRITE");
		}
		//oldarg->list[pos]->aio_nbytes = aio_return(oldarg->list[pos]);
		//oldarg->list[pos]->aio_fildes = oldarg->destfd;
		//oldarg->list[pos]->aio_lio_opcode = LIO_WRITE;
	}

	// Start Write Event After Read Event to Avoid Thread Conflicts
	//oldarg->handler->sigev_notify_function = write_aio_handler;
	//if(lio_listio(LIO_NOWAIT, oldarg->list, AIOMAX, oldarg->handler) == ERROR)
	//{
	//	strexit("WRITE LIO_LISTIO");
	//}
	//++outstanding_aio;

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

	struct aiocb** list = new struct aiocb*[AIOMAX]();
	unsigned pos, offset;
	for(pos = 0, offset = 0; pos < AIOMAX && offset <= st->st_size; ++pos, offset += PAGESIZE)
	{
		list[pos] = (struct aiocb*)malloc(sizeof(struct aiocb));
		if(!list[pos])
		{
			strexit("AIOCB MALLOC");
		}

		list[pos]->aio_fildes = srcfd;
		list[pos]->aio_buf = malloc(PAGESIZE);
		if(!list[pos]->aio_buf)
		{
			strexit("AIOCB BUFFER");
		}
		list[pos]->aio_nbytes = PAGESIZE;
		list[pos]->aio_offset = offset;
		list[pos]->aio_lio_opcode = LIO_READ;
	}

	// Setup sigevent handler for when AIO finishes
	struct sigevent* sig_evnt = (struct sigevent*) malloc(sizeof(struct sigevent));
	sig_evnt->sigev_notify = SIGEV_THREAD;
	sig_evnt->sigev_notify_function = read_aio_handler;
	sig_evnt->sigev_notify_attributes = NULL;

	// AIO Read / Write Information
	struct fileaio* arg = (struct fileaio*) malloc(sizeof(struct fileaio));
	arg->srcfd = srcfd;
	arg->destfd = destfd;
	arg->filesize = st->st_size;
	arg->list = list;
	arg->offset = offset;
	arg->count = pos;
	arg->handler = sig_evnt;

	// Set sigevnt handler argument value
	sig_evnt->sigev_value.sival_ptr = arg;

	if(lio_listio(LIO_NOWAIT, arg->list, AIOMAX, arg->handler) == ERROR)
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

