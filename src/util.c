#define _FILE_OFFSET_BITS 64
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <util.h>

// From: https://stackoverflow.com/a/46156986/13793328
// Returns the smallest of two numbers.
// (Using off_t since we're working with large files.)
static inline off_t min(off_t x, off_t y) { return (x < y) ? x : y; }

// A Utility function to getOFMDsFromFile that moves the FILE pointer to the
// first position of a string.
FILE *searchStringInFile(BYTE *small, int smallSize, int blockSize, FILE *file,
                         off_t fileSize) {
  BYTE *buffer;
  BYTE *offsetPtr;
  int smallPos;
  int origBlockSize = blockSize;
  off_t fileLeft;
  bool found;

  fileLeft = fileSize - ftello(file);
  blockSize = min(blockSize, fileLeft);

  // Initialize buffer.
  buffer = (BYTE *)malloc(blockSize * sizeof(BYTE));
  if ((fread(buffer, 1, blockSize, file)) == 0) {
    checkFileError(file);
    free(buffer);
    exit(1);
  }

  // If we are running out of file to read, just search through the last
  // chunk of the file.
  if (blockSize < origBlockSize) {
    if ((offsetPtr = my_memmem(buffer, blockSize, small, smallSize)) != NULL) {
      smallPos = (offsetPtr - buffer) - blockSize;
      fseeko(file, smallPos, SEEK_CUR);
      free(buffer);
      return file;
    } else {
      free(buffer);
      return NULL;
    }
  }

  origBlockSize = blockSize;

  // Check if 'small' exists in the current buffer.
  if ((offsetPtr = my_memmem(buffer, blockSize, small, smallSize)) != NULL) {
    // Set the position of 'small' within the buffer.
    smallPos = (offsetPtr - buffer) - blockSize;
    found = true;
  } else {
    // If 'small' doesn't exist we need to search more of the file.
    while ((offsetPtr = my_memmem(buffer, blockSize, small, smallSize)) ==
           NULL) {
      // Append more of the file to the buffer.
      // While keeping track of how big the buffer gets.
      blockSize += origBlockSize;
      buffer = (BYTE *)realloc(buffer, blockSize * sizeof(BYTE));
      if ((fread(buffer + origBlockSize, 1, origBlockSize, file)) == 0) {
        checkFileError(file);
        free(buffer);
        exit(1);
      }
      if (blockSize >= MAXCACHE) {
        printf("Oops! We've hit are memory limit searching for 3D Planes.\n");
        int lastBlockSizeMB = (float)blockSize / 1048576;
        printf("Size of search buffer before exit: %d MB\n", lastBlockSizeMB);
        free(buffer);
        return NULL;
      }
    }
    smallPos = (offsetPtr - buffer) - blockSize;
    found = true;
  }

  // Once the position is found seek to that location in the file.
  fseeko(file, smallPos, SEEK_CUR);

  // If the string in found, return the file pointer.
  if (found) {
    free(buffer);
    return file;
  } else {
    free(buffer);
    return NULL;
  }
}

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

void checkFileError(FILE *file) {
  if (ferror(file) != 0) {
    printf("Failed to read from file. Exiting.\n");
  }
  if (feof(file) != 0) {
    printf("We have reached end-of-file. Exiting.\n");
  }
}

void free2DArray(void ***array, int array2DSize) {
  for (int x = 0; x < array2DSize; x++) {
    free((*array)[x]);
  }
  free(*array);
}

// From: https://www.capitalware.com/rl_blog/?p=5847
/**
 * Function Name
 *  memmem
 *
 * Description
 *  Like strstr(), but for non-text buffers that are not NULL delimited.
 *
 *  public domain by Bob Stout
 *
 * Input parameters
 *  haystack    - pointer to the buffer to be searched
 *  haystacklen - length of the haystack buffer
 *  needle      - pointer to a buffer that will be searched for
 *  needlelen   - length of the needle buffer
 *
 * Return Value
 *  pointer to the memory address of the match or NULL.
 */
void *my_memmem(const void *haystack, size_t haystacklen, const void *needle,
                size_t needlelen) {
  /* --------------------------------------------
   * Variable declarations.
   * --------------------------------------------
   */
  char *bf = (char *)haystack, *pt = (char *)needle, *p = bf;

  /* --------------------------------------------
   * Code section
   * --------------------------------------------
   */
  while (needlelen <= (haystacklen - (p - bf))) {
    if (NULL != (p = memchr(p, (int)(*pt), haystacklen - (p - bf)))) {
      if (0 == memcmp(p, needle, needlelen))
        return p;
      else
        ++p;
    } else
      break;
  }

  return NULL;
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
