/*
 * MIT License
 *
 * Copyright (c) 2020 James McClain
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
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
