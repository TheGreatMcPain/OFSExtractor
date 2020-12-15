#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 64 Kilobytes IEC seems to be the best from my minor testing.
#define BUFFER_SIZE (1024 * 64)

int getOFMDsInFile(size_t storeSize, size_t bufferSize, const char *filename,
                   unsigned char ***OFMDs);

void *my_memmem(const void *haystack, size_t haystacklen, const void *needle,
                size_t needlelen);

// Function for freeing two dimensional arrays.
void free2DArray(void **Array2D, size_t numOfInnerArrays) {
  size_t x = 0;

  for (x = 0; x < numOfInnerArrays; x++) {
    free((Array2D)[x]);
  }

  free(Array2D);
}

int main(int argc, const char **argv) {
  int OFMDCounter = 0;
  int verified = 0;
  unsigned char **OFMDs = NULL;

  if (argc == 1) {
    fprintf(stderr, "wHy YoU nO WrItE FilEnAmE?!\n");
    exit(EXIT_FAILURE);
  }

  OFMDCounter = getOFMDsInFile(2048, BUFFER_SIZE, argv[1], &OFMDs);

  printf("OFMDs: %d\n", OFMDCounter);

  for (int x = 0; x < OFMDCounter; x++) {
    int frameRate = OFMDs[x][4] & 15;
    if (frameRate >= 1 && frameRate <= 7 && frameRate != 5) {
      verified++;
    }
  }

  printf("VerifiedOFMD: %d\n", verified);

  free2DArray((void **)OFMDs, OFMDCounter);
}

/*
 * Searches for all "valid" OFMDs within a 3D H264/MVC stream.
 * Which will be later used to create OFS '3D-Planes' files.
 *
 * 'storeSize': Size of resulting OFMD buffers.
 * 'bufferSize': Size of buffer that will be used when reading file in memory.
 * 'filename': The path to the 3D H264/MVC file.
 * 'OFMDs': Pointer to a 2D array which will contain the resulting OFMD buffers.
 */
int getOFMDsInFile(size_t storeSize, size_t bufferSize, const char *filename,
                   unsigned char ***OFMDs) {
  unsigned char seiString[4] = {0x00, 0x01, 0x06, 0x25};
  int seiSize = 4;
  int frameRate = 0;

  size_t sizeByte = sizeof(unsigned char);
  size_t sizeBypePtr = sizeof(unsigned char *);

  unsigned char *match = NULL;
  off_t matchPos = 0;
  off_t fileSize = 0;

  size_t fileRead = 0;
  size_t origBufferSize = bufferSize;
  int numOfBuffers = 0;
  // unsigned char buffer[bufferSize];
  unsigned char *buffer = (unsigned char *)malloc(sizeByte * bufferSize);
  unsigned char *bufferPtr = buffer;

  int progress = 0;
  int timeoutCounter = time(NULL);
  const int timeout = 60;

  FILE *filePtr = fopen(filename, "rb");

  // Get File Size
  fseeko(filePtr, 0, SEEK_END);
  fileSize = ftello(filePtr);
  fseeko(filePtr, 0, SEEK_SET);

  // Initilize buffer.
  fread(buffer, sizeByte, bufferSize, filePtr);

  // Find first seiString
  while ((match = my_memmem(buffer, bufferSize, seiString, seiSize)) == NULL) {
    // If no seiString was found. Just shift the buffer by half the
    // bufferSize.
    memmove(buffer, buffer + (bufferSize / 2), (bufferSize / 2) * sizeByte);
    fread(buffer + (bufferSize / 2), sizeByte, (bufferSize / 2), filePtr);
    // Stop if timeout reached.
    if ((timeoutCounter - time(NULL)) > timeout) {
      fprintf(stderr, "SEI couldn't be found within %d seconds.\n", timeout);
      return 1;
    }
  }

  while ((match = my_memmem(bufferPtr, bufferSize, seiString, seiSize)) !=
         NULL) {
    // Make match the start of buffer
    matchPos = (match - bufferPtr);
    bufferSize -= matchPos;
    bufferPtr = match;

    // if bufferSize is too small read more data
    if ((storeSize * 2) > bufferSize) {
      memmove(buffer, bufferPtr, bufferSize);
      fileRead = fread(buffer + bufferSize, sizeByte,
                       origBufferSize - bufferSize, filePtr);
      bufferSize += fileRead;
      bufferPtr = buffer;
    }

    // Search for OFMD within the next 200 bytes from the seiString.
    match = my_memmem(bufferPtr, 200, "OFMD", 4);
    if (match != NULL) {
      // Move OFMD to start of buffer.
      matchPos = (match - bufferPtr);
      bufferSize -= matchPos;
      bufferPtr = match;

      // Make sure the OFMD is valid before loading it into the OFMDs.
      frameRate = bufferPtr[4] & 15;
      if (frameRate >= 1 && frameRate <= 7 && frameRate != 5) {
        // Make room to store data into 2D array.
        *OFMDs =
            (unsigned char **)realloc(*OFMDs, sizeBypePtr * (numOfBuffers + 1));
        (*OFMDs)[numOfBuffers] = (unsigned char *)malloc(sizeByte * storeSize);
        // Copy the data to OFMDs.
        memcpy((*OFMDs)[numOfBuffers++], bufferPtr, storeSize);
      }
    } else {
      // Skip if the OFMD is not valid.
      bufferSize -= 200;
      bufferPtr += 200;
    }

    // If the next seiString can't be found.
    // Fill buffer, and search for the next seiString.
    if ((match = my_memmem(bufferPtr, bufferSize, seiString, seiSize)) ==
        NULL) {
      memmove(buffer, bufferPtr, bufferSize);
      fileRead = fread(buffer + bufferSize, sizeByte,
                       origBufferSize - bufferSize, filePtr);
      bufferSize += fileRead;
      bufferPtr = buffer;
    }

    timeoutCounter = time(NULL);
    while ((match = my_memmem(bufferPtr, bufferSize, seiString, seiSize)) ==
           NULL) {
      // Shift buffer by (bufferSize - (seiSize - 1))
      memmove(buffer, buffer + (bufferSize - (seiSize - 1)), (seiSize - 1));
      fileRead = fread(buffer + (seiSize - 1), sizeByte,
                       bufferSize - (seiSize - 1), filePtr);
      if (feof(filePtr)) {
        break;
      }
      // Stop if timeout reached.
      if ((timeoutCounter - time(NULL)) > timeout) {
        fprintf(stderr, "SEI couldn't be found within %d seconds.\n", timeout);
        return 1;
      }
    }

    progress = (float)ftello(filePtr) / (float)fileSize * 100;
    fprintf(stderr, "\rProgress: %d%s", progress, "%");
  }
  progress = (float)ftello(filePtr) / (float)fileSize * 100;
  fprintf(stderr, "\rProgress: %d%s", progress, "%");
  fprintf(stderr, "\n");
  fflush(stderr);

  free(buffer);

  return numOfBuffers;
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
