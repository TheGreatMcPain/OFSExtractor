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
#include <time.h>

#include "3dplanes.h"
#include "util.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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
                   BYTE ***OFMDs) {
  const size_t sizeByte = sizeof(unsigned char);
  const size_t sizeBypePtr = sizeof(unsigned char *);
  const size_t OFMDSearchSize = 200;
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
  int whileTimerStart = time(NULL);
  int OFMDTimerStart = time(NULL);
  const int timeout = 10;

  if ((strlen(filename) == 1) && (strncmp(filename, "-", 1) == 0)) {
    useStdin = true;
  }

  // Set stdin to binary mode.
  if (useStdin) {
#ifdef _WIN32
    // Windows doesn't except 'NULL' as parameter for freopen.
    int result = _setmode(_fileno(stdin), _O_BINARY);

    if (result == -1) {
      perror("_setmode()");
      return -1;
    }
#else
    filePtr = freopen(NULL, "rb", stdin);

    if (filePtr == NULL) {
      perror("freopen()");
      return -1;
    }
#endif
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
  fileRead = fread(buffer, sizeByte, bufferSize, filePtr);

  // Find first seiString
  while ((match = searchNative(bufferPtr, bufferSize, seiString, seiSize)) ==
         NULL) {
    // Shift buffer by (bufferSize - (seiSize - 1))
    memmove(buffer, buffer + (bufferSize - (seiSize - 1)), (seiSize - 1));
    fileRead = fread(buffer + (seiSize - 1), sizeByte,
                     bufferSize - (seiSize - 1), filePtr);
    // Stop if timeout reached.
    if ((time(NULL) - whileTimerStart) > timeout) {
      fprintf(stderr, "SEI couldn't be found within %d seconds.\n", timeout);
      return -1;
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

    if (!feof(filePtr)) {
      // if bufferSize is too small read more data
      if ((storeSize * 2) > bufferSize) {
        memmove(buffer, bufferPtr, bufferSize);
        fileRead = fread(buffer + bufferSize, sizeByte,
                         origBufferSize - bufferSize, filePtr);
        bufferSize += fileRead;
        bufferPtr = buffer;
      }
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
      // If our bufferSize gets this small we're probably done.
      if (feof(filePtr) && bufferSize <= OFMDSearchSize) {
        break;
      }
      // Skip if the OFMD is not valid.
      bufferSize -= OFMDSearchSize;
      bufferPtr += OFMDSearchSize;
    }

    // Check if OFMDCounter has increased before the timeout.
    if (OFMDCounter == 0) {
      if ((time(NULL) - OFMDTimerStart) > timeout) {
        fprintf(stderr, "No 3D-Planes found after %d seconds.\n", timeout);
        return -1;
      }
    }

    if (!feof(filePtr)) {
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

      whileTimerStart = time(NULL);
      while ((match = searchNative(bufferPtr, bufferSize, seiString,
                                   seiSize)) == NULL) {
        // Shift buffer by (bufferSize - (seiSize - 1))
        memmove(buffer, buffer + (bufferSize - (seiSize - 1)), (seiSize - 1));
        fileRead = fread(buffer + (seiSize - 1), sizeByte,
                         bufferSize - (seiSize - 1), filePtr);
        // Stop if timeout reached.
        if ((time(NULL) - whileTimerStart) > timeout) {
          fprintf(stderr, "SEI couldn't be found within %d seconds.\n",
                  timeout);
          return -1;
        }
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

// Parses the depth values from the OFMDs then stores them into the OFMDdata
// struct.
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs,
                        struct OFMDdata *OFMDdata) {
  int counter;
  int frameRate;
  int start;
  int end;
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

  OFMDdata->totalFrames = totalFrames;

  // Allocate planes array like this planes[numOfPlanes][totalFrames]
  OFMDdata->planes = (BYTE **)malloc(numOfPlanes * sizeof(BYTE *));
  for (int plane = 0; plane < numOfPlanes; plane++) {
    OFMDdata->planes[plane] = (BYTE *)malloc(totalFrames * sizeof(BYTE));
  }

  // Place the depth values into each plane.
  // Not gonna lie, it's pretty ugly looking. Depths are stored like this:
  //
  // (14 * Plane #) to (14 * Plane #) + (framecount from OFMD).
  counter = 0;
  totalFrames = 0;
  for (int OFMD = 0; OFMD < numOFMDs; OFMD++) {
    BYTE frameCount = (*OFMDs)[OFMD][11] & 127;
    for (int plane = 0; plane < numOfPlanes; plane++) {
      counter = totalFrames;
      start = 14 + (plane * frameCount);
      end = 14 + (plane * frameCount) + frameCount;
      for (int x = start; x < end; x++) {
        OFMDdata->planes[plane][counter] = (*OFMDs)[OFMD][x];
        counter++;
      }
    }
    totalFrames += frameCount;
  }
}

// Verifies each 3D-Plane, and modifies an array containing which planes are
// valid.
int verifyPlanes(struct OFMDdata OFMDdata, char *inFile) {
  int numOfPlanes = OFMDdata.numOfPlanes;
  int totalFrames = OFMDdata.totalFrames;
  BYTE **planes = OFMDdata.planes;
  int planesInFile = 0;
  const char *fileExt = getFileExt(inFile);

  for (int x = 0; x < numOfPlanes; x++) {
    bool thereArePlanes = false;
    for (int y = 0; y < totalFrames; y++) {
      if (planes[x][y] != 0x80) {
        thereArePlanes = true;
        OFMDdata.validPlanes[x] = 1; // Mark a valid plane as 1.
      }
    }

    if (thereArePlanes) {
      printf("\n");
      printf("3D-Plane #%02d\n", x);
      parseDepths(x, numOfPlanes, planes, totalFrames);
    } else {
      printf("\n");
      printf("3D-Plane #%02d is empty.\n", x);
      if (strncmp(fileExt, "m2ts", 4) == 0 ||
          strncmp(fileExt, "M2TS", 4) == 0) {
        printf("\nStopping here because input file is M2TS.\n");
        break;
      }
    }
    planesInFile++;
  }

  return planesInFile;
}

void compareDepths(int planeNum, int numOfPlanes, int totalFrames,
                   BYTE **planes) {
  char printString[80] = "Identical Planes:";
  char samePlaneStr[25];
  bool samePlane = false;

  for (int x = 0; x < numOfPlanes; x++) {
    if (memcmp(planes[planeNum], planes[x], totalFrames) == 0) {
      if (x != planeNum) {
        sprintf(samePlaneStr, " #%02d", x);
        strcat(printString, samePlaneStr);
        samePlane = true;
      }
    }
  }

  if (samePlane) {
    printf("%s\n", printString);
  } else {
    strcat(printString, " None");
    printf("%s\n", printString);
  }
}

// Prints information about the 3D-Plane.
void parseDepths(int planeNum, int numOfPlanes, BYTE **planes, int numFrames) {
  BYTE *plane = planes[planeNum];
  int minval = 128;
  int maxval = -128;
  int total = 0;
  int undefined = 0;
  int firstframe = -1;
  int lastframe = -1;
  int lastval = 0;
  int byte;
  int cuts = 0;

  for (int i = 0; i < numFrames; i++) {
    byte = plane[i];
    if (byte != lastval) {
      cuts++;
      lastval = byte;
    }
    if (byte == 128) {
      undefined += 1;
      continue;
    } else {
      lastframe = i;
      if (firstframe == -1) {
        firstframe = i;
      }
    }

    // If the value is bigger than 128. Negate the value.
    if (byte > 128) {
      byte = 128 - byte;
    }

    if (byte < minval) {
      minval = byte;
    }
    if (byte > maxval) {
      maxval = byte;
    }
    total += byte;
  }

  double average = ((double)total / ((double)numFrames - (double)undefined));
  printf("NumFrames: %d\n", numFrames);
  printf("Minimum depth: %d\n", minval);
  printf("Maximum depth: %d\n", maxval);
  printf("Average depth: %.2f\n", average);
  printf("Number of changes of depth value: %d\n", cuts);
  printf("First frame with a defined depth: %d\n", firstframe);
  printf("Last frame with a defined depth: %d\n", lastframe);
  printf("Number of frames with undefined depth: %d\n", undefined);
  compareDepths(planeNum, numOfPlanes, numFrames, planes);
  if (minval == maxval) {
    printf("*** Warning This 3D-Plane has a fixed depth of %d! ***\n", minval);
  }
}

void createOFSFiles(struct OFMDdata OFMDdata, const char *outFolder,
                    BYTE dropFrame) {
  FILE *ofsFile;
  char ofsName[80];   // Store the name of the ofs file.
  char outFile[4096]; // will become what's used with fopen.
  BYTE *buffer;
  int bufferOffset;
  // Structure of the OFS file.
  BYTE signiture[8] = {0x89, 0x4f, 0x46, 0x53, 0x0d, 0x0a, 0x1a, 0x0a};
  BYTE version[4] = {0x30, 0x31, 0x30, 0x30};
  BYTE GUID[16];
  BYTE frameRate;
  BYTE rollsAndReserved[4] = {0x01, 0x00, 0x00, 0x00};
  BYTE timecode[4] = {0x00, 0x00, 0x00, 0x00};
  BYTE frameArray[4] = {0x00, 0x00, 0x00, 0x00};
  srand(time(NULL));

  if (!dirExists(outFolder)) {
    printf("'%s' doesn't exist.\n", outFolder);
    exit(1);
  }

  // The OFS should be 41 bytes plus the number of depth values (frames).
  buffer = (BYTE *)malloc((41 * OFMDdata.totalFrames) * sizeof(BYTE));

  // Copy the signiture, and version to buffer.
  memcpy(buffer, signiture, 8);
  memcpy(buffer + 8, version, 4);

  // Generate the GUID. The last value will be the plane number.
  for (int x = 0; x < 15; x++) {
    GUID[x] = rand() % 256;
  }

  // Store the frame count in 4 bytes.
  frameArray[3] = (OFMDdata.totalFrames) % 256;
  frameArray[2] = (OFMDdata.totalFrames >> 8) % 256;
  frameArray[1] = (OFMDdata.totalFrames >> 16) % 256;
  frameArray[0] = (OFMDdata.totalFrames >> 24) % 256;

  // Calculate the framerate value.
  frameRate = (OFMDdata.frameRate * 16) + dropFrame;

  for (int plane = 0; plane < OFMDdata.numOfPlanes; plane++) {
    if (OFMDdata.validPlanes[plane] == 1) {
      bufferOffset = 12;
      strcpy(outFile, outFolder);
      GUID[15] = (BYTE)plane; // Copy the plane number to the end of the GUID.

      memcpy(buffer + bufferOffset, GUID, 16); // Copy guid
      bufferOffset += 16;
      buffer[bufferOffset] = frameRate; // Copy frame_rate value
      bufferOffset++;
      // Copy number_of_rolls, reserved, and marker_bits
      memcpy(buffer + bufferOffset, rollsAndReserved, 4);
      bufferOffset += 4;
      // Copy start_timecode (Assume 0 for now).
      memcpy(buffer + bufferOffset, timecode, 4);
      bufferOffset += 4;
      memcpy(buffer + bufferOffset, frameArray, 4); // Copy number_of_frames.
      bufferOffset += 4;
      // Copy the depth values.
      for (int x = 0; x < OFMDdata.totalFrames; x++) {
        buffer[bufferOffset] = OFMDdata.planes[plane][x];
        bufferOffset++;
      }

      // Create name for outFile.
      sprintf(ofsName, "3D-Plane-%02d.ofs", plane);
#ifdef _WIN32 // Windows uses backslashes in it's path.
      strcat(outFile, "\\");
#else
      strcat(outFile, "/");
#endif
      strcat(outFile, ofsName); // Append the file name to the output path.

      ofsFile = fopen(outFile, "wb");
      if (ofsFile == NULL) {
        printf("Failed to open: %s\n", outFile);
      }
      fwrite(buffer, 1, bufferOffset, ofsFile);
      fclose(ofsFile);
    }
  }

  free(buffer);
}
