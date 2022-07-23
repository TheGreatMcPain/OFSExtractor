/*
 * MIT License
 *
 * Copyright (c) 2021 James McClain
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
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h> // _mkdir
#endif

#include "util.h"

// Gets file extension from filename.
const char *getFileExt(const char *fileName) {
  int index = -1;
  int i = 0;

  while (fileName[i] != '\0') {
    if (fileName[i] == '.') {
      index = i;
    }
    i++;
  }

  return fileName + index + 1;
}

void free2DArray(void ***array, int array2DSize) {
  for (int x = 0; x < array2DSize; x++) {
    free((*array)[x]);
  }
  free(*array);
}

/*
 * This is slightly modified version of the 'searchNative' function
 * made by Stephan Brumme, and is under the ZLib license.
 *
 * This has been modified so that it can be a drop-in replacement for 'memmem'
 *
 * The original source code can be found...
 *
 * here: https://create.stephan-brumme.com/practical-string-searching/
 * or here: https://github.com/stbrumme/practical-string-searching
 */
void *searchNative(const void *haystack, size_t haystackLength,
                   const void *needle, size_t needleLength) {
  // uses memchr() for the first byte, then memcmp to verify it's a valid match

  // detect invalid input
  if (!haystack || !needle || haystackLength < needleLength)
    return NULL;

  // empty needle matches everything
  if (needleLength == 0)
    return (void *)haystack;

  // shorter code for just one character
  if (needleLength == 1)
    return memchr(haystack, *(const char *)needle, haystackLength);

  haystackLength -= needleLength - 1;
  // points beyond last considered byte
  const char *haystackEnd = (const char *)haystack + haystackLength;

  // look for first byte
  while ((haystack = memchr(haystack, *(const char *)needle, haystackLength)) !=
         NULL) {
    // does last byte match, too ?
    if (((const char *)haystack)[needleLength - 1] ==
        ((const char *)needle)[needleLength - 1])
      // okay, perform full comparison, skip first and last byte (if just 2
      // bytes => already finished)
      if (needleLength == 2 ||
          memcmp((const char *)haystack + 1, (const char *)needle + 1,
                 needleLength - 2) == 0)
        return (void *)haystack;

    // compute number of remaining bytes
    haystackLength = haystackEnd - (const char *)haystack;
    if (haystackLength == 0)
      return NULL;

    // keep going
    haystack = (const char *)haystack + 1;
    haystackLength--;
  }

  // needle not found in haystack
  return NULL;
}

// Checks if a directory exists.
bool dirExists(const char *path) {
  struct stat info;

  if (stat(path, &info) != 0) {
    return false;
  } else if (info.st_mode & S_IFDIR) {
    return true;
  } else {
    return false;
  }
}

// See if a file can be opened for reading.
bool testOpenReadFile(const char *filename) {
  FILE *filePtr = fopen(filename, "rb");

  if (filePtr == NULL) {
    perror("fopen()");
    printf("Failed to open '%s'\n", filename);
    return false;
  }

  fclose(filePtr);
  return true;
}

// Create a directory, but keep going if folder exists.
// return -1 if any other error occurs.
int makeDirectory(const char *path) {
#ifdef _WIN32 // Windows uses a different mkdir.
  int mkdirStatus = _mkdir(path);
#else
  int mkdirStatus = mkdir(path, 0777);
#endif

  if (errno != EEXIST) {
    if (mkdirStatus == -1) {
      printf("Failed to create '%s'\n", path);
      perror("mkdir()");
      return -1;
    }
  }

  return 0;
}

// Delete a directory.
// Ignore ENOTEMPTY errors.
int delDirectory(const char *path) {
#ifdef _WIN32
  int rmdirStatus = _rmdir(path);
#else
  int rmdirStatus = rmdir(path);
#endif

  if (errno != ENOTEMPTY) {
    if (rmdirStatus == -1) {
      printf("Failed to delete '%s'\n", path);
      perror("rmdir()");
      return -1;
    }
  }

  return 0;
}

#if _WIN32 // Windows implementation of basename.
char *basename(char *path) {
  char drive[_MAX_DRIVE];
  char dir[_MAX_DIR];
  char fname[_MAX_FNAME];
  char ext[_MAX_EXT];
  errno_t err;

  err = _splitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME,
                     ext, _MAX_EXT);

  if (err != 0) {
    printf("Error Splitting path. %d\n", err);
  }

  strcpy(path, fname);
  strcat(path, ext);

  return path;
}
#endif
