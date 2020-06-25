#define _FILE_OFFSET_BITS 64
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if !_WIN32         // If we're not on windows use POSIX basename.
#include <libgen.h> /* basename */
#endif

#if _WIN32
#include <direct.h>
char *basename(char *path); // Use our basename if on windows.
#endif

#define MAXPLANES 32
#define MAXCACHE 536870912 // Will stop if ram usage hits 512MB.

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
FILE *searchStringInFile(BYTE *small, int smallSize, int blockSize, FILE *file,
                         off_t fileSize);
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes);
void *my_memmem(const void *haystack, size_t haystacklen, const void *needle,
                size_t needlelen);
void verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes, int validPlanes[]);
void parseDepths(BYTE *plane, int numFrames);
void free2DArray(void ***array, int array2DSize);
void createOFSFiles(BYTE **planes, struct OFMDdata OFMDdata, int validPlanes[],
                    const char *outFolder);
void usage(int argc, char *argv[]);

/*
 * Start of program
 */
int main(int argc, char *argv[]) {
  BYTE **OFMDs;
  BYTE **planes;
  struct OFMDdata OFMDdata;
  int validPlanes[MAXPLANES]; // Most BluRays have 32 planes.
  int numOFMDs;
  int numOfValidPlanes;

  // Zero out validPlanes
  // to prevent it from creating false ofs files.
  for (int x = 0; x < MAXPLANES; x++) {
    validPlanes[x] = 0;
  }

  if (argc < 3) {
    usage(argc, argv);
  }

  OFMDs = (BYTE **)malloc(sizeof(BYTE *));
  numOFMDs = getOFMDsFromFile(argv[1], &OFMDs);

  if (numOFMDs == 0) {
    printf("This file doesn't have any 3D-Planes.\n");
    exit(1);
  }

  getPlanesFromOFMDs(&OFMDs, numOFMDs, &OFMDdata, &planes);

  verifyPlanes(OFMDdata, planes, validPlanes);

  createOFSFiles(planes, OFMDdata, validPlanes, argv[2]);

  // Don't leak memory!
  free2DArray((void ***)&planes, OFMDdata.numOfPlanes);
  free2DArray((void ***)&OFMDs, numOFMDs);
}

// Prints an array in hex.
// For debugging REMOVE LATER
void print_hex_memory(void *mem) {
  int i;
  unsigned char *p = (unsigned char *)mem;
  for (i = 0; i < 128; i++) {
    printf("0x%02x ", p[i]);
    if ((i % 16 == 0) && i)
      printf("\n");
  }
  printf("\n");
}

void usage(int argc, char *argv[]) {
  char *program = basename(argv[0]);

  printf("Usage: %s <input.mvc> <output folder>\n", program);
  exit(0);
}

void createOFSFiles(BYTE **planes, struct OFMDdata OFMDdata, int validPlanes[],
                    const char *outFolder) {
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
  BYTE dropframe = 0;
  BYTE rollsAndReserved[4] = {0x01, 0x00, 0x00, 0x00};
  BYTE timecode[4] = {0x00, 0x00, 0x00, 0x00};
  BYTE frameArray[4] = {0x00, 0x00, 0x00, 0x00};
  srand(time(NULL));
  struct stat sb;

  if (stat(outFolder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    printf("Output folder exists!.\n");
  } else {
#ifdef _WIN32 // Windows uses a different mkdir.
    _mkdir(outFolder);
#else
    mkdir(outFolder, 0777);
#endif
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
  frameRate = (OFMDdata.frameRate * 16) + dropframe;

  for (int plane = 0; plane < OFMDdata.numOfPlanes; plane++) {
    if (validPlanes[plane] == 1) {
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
        buffer[bufferOffset] = planes[plane][x];
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

void free2DArray(void ***array, int array2DSize) {
  for (int x = 0; x < array2DSize; x++) {
    free((*array)[x]);
  }
  free(*array);
}

// Collects the OFMD data from the MVC file, and stores it in a 2D array.
int getOFMDsFromFile(const char *fileName, BYTE ***OFMDs) {
  FILE *filePtr;
  BYTE *posPtr;
  off_t OFMDpos;
  BYTE *buffer;
  const int blockSize = 524288;
  BYTE seiString[4] = {0x00, 0x01, 0x06, 0x25};
  int numOFMDs = 0;
  BYTE frameRate;

  filePtr = fopen(fileName, "rb");
  if (filePtr == NULL) {
    printf("Failed to open %s.\n", fileName);
    exit(1);
  }
  // Get the size of the file.
  fseeko(filePtr, 0, SEEK_END);
  off_t fileSize = ftello(filePtr);
  fseeko(filePtr, 0, SEEK_SET);

  while (1) {
    // Search for the next SEI position in the file.
    if (searchStringInFile(seiString, 4, blockSize, filePtr, fileSize) !=
        NULL) {
      // Check for nearby OFMD.
      buffer = (BYTE *)malloc(200 * sizeof(BYTE));
      fread(buffer, 1, 200, filePtr);
      if ((posPtr = my_memmem(buffer, 200, "OFMD", 4)) != NULL) {
        // Seek to OFMD
        OFMDpos = (posPtr - buffer) - 200;
        fseeko(filePtr, OFMDpos, SEEK_CUR);
        // printf("%ld\n", OFMDpos);
      } else {
        free(buffer);
        continue;
      }

      // Store the OFMD's data into buffer.
      buffer = (BYTE *)malloc(4096 * sizeof(BYTE));
      fread(buffer, 1, 4096, filePtr);
      fseeko(filePtr, -4096, SEEK_CUR);

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
    int status = ceil(((double)ftello(filePtr) / (double)fileSize) * 100);
    // Print to stderr incase we want to pipe to a file.
    fprintf(stderr, "\rProgress %d%s", status, "%");
    fflush(stderr);
  }

  fclose(filePtr);
  printf("\n");
  return numOFMDs;
}

// Parses the depth values from the OFMDs then stores them into the OFMDdata
// struct.
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes) {
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
  *planes = (BYTE **)malloc(numOfPlanes * sizeof(BYTE *));
  for (int plane = 0; plane < numOfPlanes; plane++) {
    (*planes)[plane] = (BYTE *)malloc(totalFrames * sizeof(BYTE));
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
        (*planes)[plane][counter] = (*OFMDs)[OFMD][x];
        counter++;
      }
    }
    totalFrames += frameCount;
  }
}

// Verifies each 3D-Plane, and modifies an array containing which planes are
// valid.
void verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes, int validPlanes[]) {
  int frameRate = OFMDdata.frameRate;
  int numOfPlanes = OFMDdata.numOfPlanes;
  int totalFrames = OFMDdata.totalFrames;
  float fps;

  switch (frameRate) {
  case 1:
    fps = 23.976;
    break;
  case 2:
    fps = 24;
    break;
  case 3:
    fps = 25;
    break;
  case 4:
    fps = 29.97;
  case 6:
    fps = 50;
    break;
  case 7:
    fps = 60;
    break;
  default:
    printf("FrameRate is invalid\n");
  }

  for (int x = 0; x < numOfPlanes; x++) {
    bool thereArePlanes = false;
    for (int y = 0; y < totalFrames; y++) {
      if (planes[x][y] != 0x80) {
        thereArePlanes = true;
        validPlanes[x] = 1; // Mark a valid plane as 1.
      }
    }

    if (thereArePlanes) {
      printf("\n");
      printf("3D-Plane #%d\n", x);
      parseDepths(planes[x], totalFrames);
    } else {
      printf("\n");
      printf("3D-Plane #%d is Empty!\n", x);
    }
  }
}

// Prints information about the 3D-Plane.
void parseDepths(BYTE *plane, int numFrames) {
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
  printf("Undefined: %d\n", undefined);
  if (minval == maxval) {
    printf("*** Warning This 3D-Plane has a fixed depth of minval! ***");
  }
}

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
  fread(buffer, 1, blockSize, file);

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
      fread(buffer + origBlockSize, 1, origBlockSize, file);
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
