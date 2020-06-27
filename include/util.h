#include <stdio.h>
#include <stdlib.h>

#define BLOCKSIZE 131072   // 128KB
#define MAXCACHE 536870912 // Will stop if ram usage hits 512MB.

typedef unsigned char BYTE;

#if _WIN32
char *basename(char *path);
#else
#include "libgen.h"
#endif

FILE *searchStringInFile(BYTE *small, int smallSize, int blockSize, FILE *file,
                         off_t fileSize);
void *my_memmem(const void *haystack, size_t haystacklen, const void *needle,
                size_t needlelen);
void free2DArray(void ***array, int array2DSize);
void checkFileError(FILE *file);
const char *getFileExt(const char *fileName);
