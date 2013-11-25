#include <sys/time.h>
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

#define BUFSIZE 16
#define ARGSNUM 3
#define BTSIZE 10
#define ERROR -1
#define NOPENFD 50

std::string* srcfilepath;
std::string* destfilepath;

void cleanup()
{
	free(srcfilepath);
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

int copy_file(const char *srcpath, const char *destpath)
{
	const unsigned PAGESIZE = getpagesize() * BUFSIZE;
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

	char* buffer = (char*) malloc(PAGESIZE);
	for(unsigned i = 0; i < st->st_size; i += PAGESIZE)
	{
		int dataread = read(srcfd, buffer, PAGESIZE);	
		if(dataread == ERROR)
		{ 
			strexit("READ");
		}

		if(write(destfd, buffer, dataread) == ERROR)
		{
			strexit("WRITE");
		}
	}
	free(st);
	free(buffer);
	close(srcfd);
	close(destfd);
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

	//printf("%s\n", (*destfilepath + filepathname(std::string(fpath), ftwbuf->level)).c_str());	
	copy_file(fpath, (*destfilepath + filepathname(std::string(fpath), ftwbuf->level)).c_str());	
	return 0;           /* To tell nftw() to continue */
}

int main(int argc, char *argv[])
{
        struct timeval start_time;
        struct timeval end_time;
        gettimeofday(&start_time, NULL);

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
	cleanup();
        gettimeofday(&end_time, NULL);
        double seconds = difftime(end_time.tv_sec, start_time.tv_sec);
	printf("Time Elapsed: %f seconds\n", seconds);
	exit(EXIT_SUCCESS);
}

