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

// From: https://create.stephan-brumme.com/practical-string-searching/
void *searchNative(const void *haystack, size_t haystackLength,
                   const void *needle, size_t needleLength);

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
  const size_t sizeByte = sizeof(unsigned char);
  const size_t sizeBypePtr = sizeof(unsigned char *);
  const size_t OFMDSearchSize = 400;
  int frameRate = 0;
  int OFMDCounter = 0;

  const unsigned char seiString[4] = {0x00, 0x01, 0x06, 0x25};
  const size_t seiSize = 4;

  FILE *filePtr;
  off_t fileSize = 0;
  size_t fileRead = 0;
  bool useStdin = false;
  unsigned char *buffer = (unsigned char *)malloc(sizeByte * bufferSize);
  unsigned char *bufferPtr = buffer;
  size_t origBufferSize = bufferSize;
  unsigned char *match = NULL;

  int progress = 0;
  int timeoutCounter = time(NULL);
  const int timeout = 60;

  if ((strlen(filename) == 1) && (strncmp(filename, "-", 1) == 0)) {
    useStdin = true;
  }

  if (useStdin) {
    filePtr = stdin;
  } else {
    filePtr = fopen(filename, "rb");
  }

  if (!useStdin) {
    // Get File Size
    fseeko(filePtr, 0, SEEK_END);
    fileSize = ftello(filePtr);
    fseeko(filePtr, 0, SEEK_SET);
  }

  // Initilize buffer.
  fread(buffer, sizeByte, bufferSize, filePtr);

  // Find first seiString
  while ((match = searchNative(bufferPtr, bufferSize, seiString, seiSize)) ==
         NULL) {
    // Shift buffer by (bufferSize - (seiSize - 1))
    memmove(buffer, buffer + (bufferSize - (seiSize - 1)), (seiSize - 1));
    fileRead = fread(buffer + (seiSize - 1), sizeByte,
                     bufferSize - (seiSize - 1), filePtr);
    // Stop if timeout reached.
    if ((timeoutCounter - time(NULL)) > timeout) {
      fprintf(stderr, "SEI couldn't be found within %d seconds.\n", timeout);
      return 1;
    }
  }
  bufferSize -= (match - bufferPtr);
  bufferPtr = match;

  memmove(buffer, bufferPtr, bufferSize);
  fileRead = fread(buffer + bufferSize, sizeByte, origBufferSize - bufferSize,
                   filePtr);
  bufferSize += fileRead;
  bufferPtr = buffer;

  while ((match = searchNative(bufferPtr, bufferSize, seiString, seiSize)) !=
         NULL) {
    // Make match the start of buffer
    bufferSize -= (match - bufferPtr);
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
    match = searchNative(bufferPtr, OFMDSearchSize, "OFMD", 4);
    if (match != NULL) {
      // Move OFMD to start of buffer.
      bufferSize -= (match - bufferPtr);
      bufferPtr = match;

      // Make sure the OFMD is valid before loading it into the OFMDs.
      frameRate = bufferPtr[4] & 15;
      if (frameRate >= 1 && frameRate <= 7 && frameRate != 5) {
        // Make room to store data into 2D array.
        *OFMDs =
            (unsigned char **)realloc(*OFMDs, sizeBypePtr * (OFMDCounter + 1));
        (*OFMDs)[OFMDCounter] = (unsigned char *)malloc(sizeByte * storeSize);
        // Copy the data to OFMDs.
        memcpy((*OFMDs)[OFMDCounter++], bufferPtr, storeSize);
      }
    } else {
      // Skip if the OFMD is not valid.
      bufferSize -= OFMDSearchSize;
      bufferPtr += OFMDSearchSize;
    }

    // If the next seiString can't be found.
    // Fill buffer, and search for the next seiString.
    if ((match = searchNative(bufferPtr, bufferSize, seiString, seiSize)) ==
        NULL) {
      memmove(buffer, bufferPtr, bufferSize);
      fileRead = fread(buffer + bufferSize, sizeByte,
                       origBufferSize - bufferSize, filePtr);
      bufferSize += fileRead;
      bufferPtr = buffer;
    }

    timeoutCounter = time(NULL);
    while ((match = searchNative(bufferPtr, bufferSize, seiString, seiSize)) ==
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

    if (!useStdin) {
      progress = (float)ftello(filePtr) / (float)fileSize * 100;
      fprintf(stderr, "\rProgress: %d%s", progress, "%");
    }
  }

  if (!useStdin) {
    progress = (float)ftello(filePtr) / (float)fileSize * 100;
    fprintf(stderr, "\rProgress: %d%s", progress, "%");
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  free(buffer);

  if (!useStdin) {
    fclose(filePtr);
  }

  return OFMDCounter;
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
