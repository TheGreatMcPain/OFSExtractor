#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function definitions.
FILE *searchStringInFile(char *small, int smallSize, int blockSize, FILE *file,
                         size_t fileSize);
void *my_memmem(const void *big, size_t big_len, const void *little,
                size_t little_len);
static inline size_t min(size_t x, size_t y);

// Globals
int BLOCK = 1048576;

int main(int argc, char *argv[]) {
  FILE *filePtr;
  char *buffer;

  filePtr = fopen(argv[1], "r");
  fseek(filePtr, 0L, SEEK_END);
  size_t fileSize = ftell(filePtr);
  fseek(filePtr, 0L, SEEK_SET);

  while (1) {
    if (searchStringInFile("OFMD", 4, BLOCK, filePtr, fileSize) != NULL) {
      // Replicate output of grep.
      printf("%ld:OFMD\n", ftell(filePtr));
    } else {
      break;
    }
    fseek(filePtr, 1, SEEK_CUR);
  }
}

FILE *searchStringInFile(char *small, int smallSize, int blockSize, FILE *file,
                         size_t fileSize) {
  char *buffer;
  char *offsetPtr;
  int smallPos;
  int origBlockSize = blockSize;
  size_t fileLeft;
  bool found;

  fileLeft = fileSize - ftell(file);
  blockSize = min(blockSize, fileLeft);

  // Initialize buffer.
  buffer = (char *)malloc(blockSize * sizeof(char));
  fread(buffer, 1, blockSize, file);

  if (blockSize < origBlockSize) {
    if ((offsetPtr = my_memmem(buffer, blockSize, small, smallSize)) != NULL) {
      smallPos = offsetPtr - buffer;
      fseek(file, -blockSize, SEEK_CUR);
      fseek(file, smallPos, SEEK_CUR);
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
    smallPos = offsetPtr - buffer;
    found = true;
  } else {
    // If 'small' doesn't exist we need to search more of the file.
    while ((offsetPtr = my_memmem(buffer, blockSize, small, smallSize)) ==
           NULL) {
      // Append more of the file to the buffer.
      // While keeping track of how big the buffer gets.
      blockSize += origBlockSize;
      buffer = (char *)realloc(buffer, blockSize * sizeof(char));
      fread(buffer + origBlockSize, 1, origBlockSize, file);
    }
    smallPos = offsetPtr - buffer;
    found = true;
  }

  // Once the position is found seek to that location in the file.
  fseek(file, -blockSize, SEEK_CUR);
  fseek(file, smallPos, SEEK_CUR);

  // If the string in found, return the file pointer.
  if (found) {
    free(buffer);
    return file;
  } else {
    free(buffer);
    return NULL;
  }
}

// From: https://stackoverflow.com/a/46156986/13793328
static inline size_t min(size_t x, size_t y) { return (x < y) ? x : y; }

// From: https://stackoverflow.com/a/30683927/13793328
void *my_memmem(const void *big, size_t big_len, const void *little,
                size_t little_len) {
  void *iterator;
  if (big_len < little_len)
    return NULL;

  iterator = (void *)big;
  while (1) {
    iterator = memchr(iterator, ((unsigned char *)little)[0],
                      big_len - (iterator - big));
    if (iterator == NULL)
      return NULL;
    if (iterator && !memcmp(iterator, little, little_len))
      return iterator;
    iterator++;
  }
}
