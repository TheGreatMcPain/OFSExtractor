#define FILE_OFFSET_BITS 64
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char BYTE;

/*
 * Structs
 */
struct OFMDdata {
  int frameRate;
  int totalFrames;
  int numOfPlanes;
};

/*
 * Function definitions.
 */
int getOFMDsFromFile(const char *fileName, BYTE ***OFMDs);
FILE *searchStringInFile(char *small, int smallSize, int blockSize, FILE *file,
                         size_t fileSize);
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes);
void *my_memmem(const void *big, size_t big_len, const void *little,
                size_t little_len);
static inline size_t min(size_t x, size_t y);
int verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes);

/*
 * Start of program
 */
int main(int argc, char *argv[]) {
  BYTE **OFMDs;
  BYTE **planes;
  struct OFMDdata OFMDdata;
  int numOFMDs;
  int numOfValidPlanes;

  OFMDs = (BYTE **)malloc(sizeof(BYTE *));
  numOFMDs = getOFMDsFromFile(argv[1], &OFMDs);

  getPlanesFromOFMDs(&OFMDs, numOFMDs, &OFMDdata, &planes);

  for (int x = 0; x < OFMDdata.totalFrames; x++) {
    printf("%d\n", planes[0][x]);
  }

  printf("%d\n", OFMDdata.totalFrames);

  // numOfValidPlanes = verifyPlanes(OFMDdata, planes);
}

// Collects the OFMD data from the MVC file, and stores it in a 2D array.
int getOFMDsFromFile(const char *fileName, BYTE ***OFMDs) {
  FILE *filePtr;
  BYTE *buffer;
  int blockSize = 1048576;
  int numOFMDs = 0;
  BYTE frameRate;

  filePtr = fopen(fileName, "rb");
  // Get the size of the file.
  fseek(filePtr, 0L, SEEK_END);
  size_t fileSize = ftell(filePtr);
  fseek(filePtr, 0L, SEEK_SET);

  while (1) {
    // Search for the next OFMD position in the file.
    if (searchStringInFile("OFMD", 4, blockSize, filePtr, fileSize) != NULL) {
      // Store the OFMD's data into buffer.
      buffer = (BYTE *)malloc(4096 * sizeof(BYTE));
      fread(buffer, 1, 4096, filePtr);
      fseek(filePtr, -4096, SEEK_CUR);

      // Verify the OFMD by checking it's frame-rate value.
      frameRate = buffer[4] & 15;
      if (frameRate >= 1 && frameRate <= 7) {
        // If the OFMD is valid create a new pointer to store it's data.
        *OFMDs = (BYTE **)realloc(*OFMDs, (numOFMDs + 1) * sizeof(BYTE *));
        (*OFMDs)[numOFMDs] = buffer;
        numOFMDs += 1;
      } else {
        free(buffer);
      }

    } else {
      // Break the loop if we're done searching.
      break;
    }
    // double status = ((double)ftell(filePtr) / (double)fileSize) * 100;
    // printf("%f\n", status);
    // Prepare to search for the next OFMD.
    fseek(filePtr, 1, SEEK_CUR);
  }

  return numOFMDs;
}

// Parses the depth values from the OFMDs then stores them into the OFMDdata
// struct.
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes) {
  BYTE frameRate;
  int numOfPlanes;
  int totalFrames = 0;

  // Let's hope the frame-rate, and the number of planes don't change
  frameRate = (*OFMDs)[0][4] & 15;

  numOfPlanes = (*OFMDs)[0][10] & 0x7F;

  OFMDdata->numOfPlanes = numOfPlanes;
  OFMDdata->frameRate = frameRate;

  // Get total number of frames.
  for (int OFMD = 0; OFMD < numOFMDs; OFMD++) {
    totalFrames += (*OFMDs)[OFMD][11] & 127;
  }

  *planes = (BYTE **)malloc(1 * sizeof(BYTE *));
  (*planes)[0] = (BYTE *)malloc(totalFrames * sizeof(BYTE));

  OFMDdata->totalFrames = totalFrames;

  int counter = 0;
  for (int OFMD = 0; OFMD < numOFMDs; OFMD++) {
    BYTE frameCount = (*OFMDs)[OFMD][11] & 127;
    for (int x = 14; x < (14 + frameCount); x++) {
      (*planes)[0][counter] = (*OFMDs)[OFMD][x];
      counter++;
      // printf("%d\n", (*planes)[0][x]);
    }
  }
}

int verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes) {
  int frameRate = OFMDdata.frameRate;
  int numOfPlanes = OFMDdata.numOfPlanes;
  int numOfValidPlanes = 0;
  int totalFrames = OFMDdata.totalFrames;

  for (int x = 0; x < numOfPlanes; x++) {
    bool thereArePlanes = false;
    for (int y = 0; y < totalFrames; y++) {
      if (planes[x][y] != 0x80) {
        thereArePlanes = true;
        printf("%d\n", planes[x][y]);
      }
    }

    if (thereArePlanes) {
      printf("\n");
      printf("3D-Plane #%d\n", x);
      printf("Has Depths!\n");
      numOfValidPlanes++;
    } else {
      printf("\n");
      printf("3D-Plane #%d\n is Empty!", x);
    }
  }

  return numOfValidPlanes;
}

// A Utility function to getOFMDsFromFile that moves the FILE pointer to the
// first position of a string.
FILE *searchStringInFile(char *small, int smallSize, int blockSize, FILE *file,
                         size_t fileSize) {
  BYTE *buffer;
  BYTE *offsetPtr;
  int smallPos;
  int origBlockSize = blockSize;
  size_t fileLeft;
  bool found;

  fileLeft = fileSize - ftell(file);
  blockSize = min(blockSize, fileLeft);

  // Initialize buffer.
  buffer = (BYTE *)malloc(blockSize * sizeof(BYTE));
  fread(buffer, 1, blockSize, file);

  // If we are running out of file to read, just search through the last
  // chunk of the file.
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
      buffer = (BYTE *)realloc(buffer, blockSize * sizeof(BYTE));
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
// Returns the smallest of two numbers.
static inline size_t min(size_t x, size_t y) { return (x < y) ? x : y; }

// From: https://stackoverflow.com/a/30683927/13793328
// A more portable solution to memmem.
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
