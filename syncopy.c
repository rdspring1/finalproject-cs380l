#define _XOPEN_SOURCE 500
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

#define ARGSNUM 3
#define BTSIZE 10
#define ERROR -1
#define NOPENFD 50

std::string* destfilepath;

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
	exit(EXIT_FAILURE);
}

bool samefile(int fd1, int fd2)
{
	struct stat stat1, stat2;
	if(fstat(fd1, &stat1) == ERROR) strexit("FSTAT");
	if(fstat(fd2, &stat2) == ERROR) strexit("FSTAT");
	return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}


static int display_info(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
	printf("%-3s %2d %7jd   %-40s %d %s\n",
			(tflag == FTW_D) ?   "d"   : (tflag == FTW_DNR) ? "dnr" :
			(tflag == FTW_DP) ?  "dp"  : (tflag == FTW_F) ?   "f" :
			(tflag == FTW_NS) ?  "ns"  : (tflag == FTW_SL) ?  "sl" :
			(tflag == FTW_SLN) ? "sln" : "???",
			ftwbuf->level, (intmax_t) sb->st_size,
			fpath, ftwbuf->base, fpath + ftwbuf->base);
	return 0;           /* To tell nftw() to continue */
}

std::string filepathname(const std::string& str, int level)
{
	size_t pos = str.length();
	for(int i = 0; i < level; ++i)
	{
		pos = str.find_last_of("/\\", pos-1);
	}
	return str.substr(pos);
}

int copy_file(const char *srcpath, const char *destpath)
{
	const unsigned PAGESIZE = getpagesize();
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

	int destfd = open(destpath, O_WRONLY | O_CREAT | O_APPEND, st->st_mode);
	if(destfd == ERROR)
	{
		strexit(destpath);
	}

	if(samefile(srcfd, destfd))
	{
		return true;
	}
	else if(ftruncate(destfd, 0) == ERROR)
	{
		strexit("TRUNCATE");
	}

	char* buffer = (char*) malloc(PAGESIZE);
	for(unsigned i = 0; i < st->st_size; i += PAGESIZE)
	{
		if(read(srcfd, buffer, PAGESIZE) == ERROR)
		{ 
			strexit("READ");
		}
		
		if(write(destfd, buffer, PAGESIZE) == ERROR)
		{
			strexit("WRITE");
		}
	}
	free(st);
	free(buffer);
	close(srcfd);
	return destfd;
}

int main(int argc, char *argv[])
{
	if(argc != ARGSNUM)	
	{
		strexit("Missing Operands");
	}

	struct stat* srcst = (struct stat*) malloc(sizeof(struct stat));
	if(stat(argv[1], srcst) == ERROR) 
	{ 
		strexit("SRC FSTAT");
	}
	
	struct stat* destst = (struct stat*) malloc(sizeof(struct stat));
	if(stat(argv[2], destst) == ERROR) 
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
				if(mkdir(argv[2], S_IRWXU | S_IRWXG) == ERROR)
				{
					strexit("MKDIR");
				}
				// TODO: copy_directory
			}
			else
			{
				copy_file(argv[1], argv[2]);
			}
			exit(EXIT_SUCCESS);
		}
	}

	if(!S_ISDIR(srcst->st_mode)) 
	{
		if(!S_ISDIR(destst->st_mode)) // SRC-FILE DEST-FILE
		{
			copy_file(argv[1], argv[2]);
		}
		else // SRC-FILE DEST-DIRECTORY
		{
			copy_file(argv[1], (std::string(argv[2]) + filepathname(std::string(argv[1]), 1)).c_str());
		}
	}
	else if(S_ISDIR(destst->st_mode)) // SRC-DIRECTORY AND DEST-DIRECTORY
	{
		if(mkdir((std::string(argv[2]) + filepathname(std::string(argv[1]), 1)).c_str(), S_IRWXU | S_IRWXG) == ERROR)
		{
			strexit("MKDIR");
		}
		// TODO: copy_directory
	}
	else
	{
		strexit("Cannot copy directory to file");
	}

	//printf("%s\n", (std::string(argv[2]) + filepathname(std::string(argv[1]), 2)).c_str());
	//int flags = 0;
	//if (nftw(argv[1], display_info, 50, FTW_PHYS | FTW_DEPTH == -1)) 
	//{
	//	exit(EXIT_FAILURE);
	//}
	exit(EXIT_SUCCESS);
}

