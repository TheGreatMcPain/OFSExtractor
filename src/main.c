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
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dplanes.h"
#include "compiledate.h" // Generated via meson
#include "util.h"
#include "version.h" // from 'git describe --tags --dirty=+'

#ifndef VERSION
#define VERSION "1.0"
#endif

#if __x86_64__
#define ARCH "64bit "
#elif __i386__
#define ARCH "32bit "
#endif

void parseOptions(int argc, char *argv[], BYTE *newFrameRate, BYTE *dropFrame,
                  char **outFolder);
void printLicense();
void usage(char *argv[]);
void printIntro();
char *printFpsValue(int frameRate);

int main(int argc, char *argv[]) {
  BYTE **OFMDs;
  BYTE newFrameRate = 0;
  BYTE dropFrame = 0;
  struct OFMDdata OFMDdata;
  int validPlanes[MAXPLANES]; // Most BluRays have 32 planes.
  int planesInFile;
  int planesWritten = 0;
  int numOFMDs;
  char *outFolder;

  // Prevent creating false ofs files.
  for (int x = 0; x < MAXPLANES; x++) {
    validPlanes[x] = 0;
  }

  printIntro();
  parseOptions(argc, argv, &newFrameRate, &dropFrame, &outFolder);

  if (newFrameRate > 0) {
    OFMDdata.frameRate = newFrameRate;
  }

  // Create OFS file directory.
  if (makeDirectory(outFolder) == -1) {
    exit(1);
  }

  printf("Searching file for 3D-Planes.\n\n");

  OFMDs = (BYTE **)malloc(sizeof(BYTE *));
  numOFMDs = getOFMDsInFile(OFMD_SIZE, BUFFER_SIZE, argv[1], &OFMDs);

  // if 'getOFMDsInFile' returns -1 it failed to open input file.
  if (numOFMDs == -1) {
    exit(1);
  }

  if (numOFMDs == 0) {
    printf("This file doesn't have any 3D-Planes.\n");
    exit(1);
  }

  getPlanesFromOFMDs(&OFMDs, numOFMDs, &OFMDdata);

  printf("\nChecking 3D-Planes for valid depth values.\n");

  planesInFile = verifyPlanes(OFMDdata, validPlanes, argv[1]);
  createOFSFiles(OFMDdata, validPlanes, outFolder, dropFrame);

  for (int x = 0; x < MAXPLANES; x++) {
    if (validPlanes[x] == 1) {
      planesWritten++;
    }
  }

  // Don't leak memory!
  free2DArray((void ***)&OFMDdata.planes, OFMDdata.numOfPlanes);
  free2DArray((void ***)&OFMDs, numOFMDs);

  printf("\nNumber of 3D-Planes in MVC stream: %d\n", planesInFile);
  printf("Number of 3D-Planes written: %d\n", planesWritten);
  printf("Number of frames: %d\n", OFMDdata.totalFrames);
  printf("Framerate: %s\n\n", printFpsValue(OFMDdata.frameRate));
}

char *printFpsValue(int frameRate) {
  char *fpsString;

  switch (frameRate) {
  case 1:
    fpsString = "23.976";
    break;
  case 2:
    fpsString = "24";
    break;
  case 3:
    fpsString = "25";
    break;
  case 4:
    fpsString = "29.97";
    break;
  case 6:
    fpsString = "50";
    break;
  case 7:
    fpsString = "59.94";
    break;
  default:
    fpsString = "23.976";
  }

  return fpsString;
}

// Exits program if frame-rate value is invalid.
void isValidFps(int frameRate) {
  BYTE frameRateOption[6] = {1, 2, 3, 4, 6, 7};
  bool validFps = false;
  for (int x = 0; x < 6; x++) {
    if (frameRate == frameRateOption[x]) {
      printf("OFS Framerate will now be: %s\n\n", printFpsValue(frameRate));
      validFps = true;
      break;
    }
  }
  if (!validFps) {
    printf("'-fps %d' is invalid. Value must be ", frameRate);
    printf("between 1 and 4, 6, or 7.\n");
    exit(1);
  }
}

// Helper function for 'parseOptions'
bool checkFileExt(const char **validExts, size_t validExtsSize,
                  const char *inExt) {
  char lowerExt[5] = "\0"; // Will increase this if need be.
  size_t x;

  // Make inExt lowercase.
  for (x = 0; x < strlen(inExt); x++) {
    lowerExt[x] = tolower(inExt[x]);
  }

  // compare with validExt
  for (x = 0; x < validExtsSize; x++) {
    if (strcmp(validExts[x], lowerExt) == 0) {
      return true;
    }
  }

  return false;
}

void parseOptions(int argc, char *argv[], BYTE *newFrameRate, BYTE *dropFrame,
                  char **outFolder) {
  char *supportedExt[4] = {"mvc", "h264", "264", "m2ts"};
  const char *fileExt;

  if (argc >= 2) {
    if (strncmp(argv[1], "-license", 8) == 0) {
      printLicense();
      exit(0);
    }
  }

  if (argc >= 2) {
    if ((strlen(argv[1]) != 1) && (strncmp(argv[1], "-", 1) != 0)) {
      if (testOpenReadFile(argv[1])) {
        // Check if file extention is supported.
        fileExt = getFileExt(argv[1]);
        if (!checkFileExt((const char **)supportedExt, 4, fileExt)) {
          printf("'%s': Is not a supported file extention.\n", fileExt);
          exit(1);
        }
      } else {
        // Exit if input file can't be opened.
        exit(1);
      }
    }
  }

  if (argc == 1) {
    usage(argv);
  } else if (argc == 2) {
    *outFolder = "."; // Output to current if option not set.
  } else if (argc == 3) {
    if (strncmp(argv[2], "-fps", 4) == 0) {
      printf("'-fps' requires a value.\n");
      exit(1);
    } else {
      *outFolder = argv[2]; // Set output Folder.
    }
  } else if (argc == 4) {
    if (strncmp(argv[3], "-fps", 4) == 0) {
      printf("'-fps' requires a value.\n");
      exit(1);
    } else if (strncmp(argv[2], "-fps", 4) == 0) {
      sscanf(argv[3], "%d", (int *)&(*newFrameRate));
      isValidFps(*newFrameRate);
      *outFolder = ".";
    } else {
      printf("Invalid input!\n");
      exit(1);
    }
  } else if (argc == 5) {
    if (strncmp(argv[2], "-fps", 4) == 0) {
      sscanf(argv[3], "%d", (int *)&(*newFrameRate)); // Convert to int
      isValidFps(*newFrameRate);
      if (strncmp(argv[4], "-dropframe", 10) == 0) {
        if (*newFrameRate == 4) {
          *dropFrame = 1;
          printf("'drop_frame_flag' will be set in OFS.\n\n");
        } else {
          printf("'-dropframe' is only compatible with '-fps 4'.\n");
          exit(1);
        }
        *outFolder = ".";
      } else {
        printf("Invalid input!\n");
        exit(1);
      }
    } else if (strncmp(argv[3], "-fps", 4) == 0) {
      sscanf(argv[4], "%d", (int *)&(*newFrameRate)); // Convert to int
      isValidFps(*newFrameRate);
      *outFolder = argv[2];
    } else {
      printf("Invalid input!\n");
      exit(1);
    }
  } else if (argc == 6) {
    if (strncmp(argv[3], "-fps", 4) == 0) {
      sscanf(argv[4], "%d", (int *)&(*newFrameRate));
      isValidFps(*newFrameRate);
      if (strncmp(argv[5], "-dropframe", 10) == 0) {
        if (*newFrameRate == 4) {
          *dropFrame = 1;
          printf("'drop_frame_flag' will be set in OFS.\n\n");
        } else {
          printf("'-dropframe' is only compatible with '-fps 4'.\n");
          exit(1);
        }
      } else {
        printf("Invalid input!\n");
        exit(1);
      }
      *outFolder = argv[2];
    } else {
      printf("Invalid input!\n");
      exit(1);
    }
  }
}

void printIntro() {
  // Print version info, and 'arch'.
  printf("OFSExtractor %s %sby TheGreatMcPain (aka Sixsupersonic on doom9)\n",
         VERSION, ARCH);
#ifndef DATE // Set compile date.
  printf("This program was compiled on %s %s.\n\n", __DATE__, __TIME__);
#else
  printf("This program was compiled on %s.\n\n", DATE);
#endif
}

void usage(char *argv[]) {
  char *program = basename(argv[0]);

  printf("Usage: %s [-license] <input file> <output folder> [-fps # "
         "-dropframe] \n\n",
         program);
  printf("  -license : Print license (MIT).\n\n");
  printf("  <input file> : Can be a raw MVC stream, ");
  printf("a H264+MVC combined stream (like those from MakeMKV),\n");
  printf("                 or a M2TS file. (M2TS is not fully supported.)\n");
  printf("                 Using '-' will read from stdin.\n\n");
  printf("  <output folder> : The output folder which will contain the ofs "
         "files.\n");
  printf("                    If undefined the current directory will be "
         "used.\n\n");
  printf("Advanced Options: Use with care!\n\n");
  printf("  -fps # : Must be value between 1 and 4, 6, or 7. See Table.\n");
  printf("           This will override the fps value that would normally come "
         "from the MVC stream.\n\n");
  printf("           FPS Conversion Table:\n");
  printf("           1 : 23.976 fps\n");
  printf("           2 : 24 fps\n");
  printf("           3 : 25 fps\n");
  printf("           4 : 29.97 fps\n");
  printf("           6 : 50 fps\n");
  printf("           7 : 59.94 fps\n\n");
  printf(
      "  -dropframe : Set drop_frame_flag within the resulting OFS files.\n");
  printf("               Can only be used with FPS value 4.\n\n");
  exit(0);
}

void printLicense() {
  printf("MIT License\n\n");
  printf("Copyright (c) 2020 James McClain\n\n");

  printf("Permission is hereby granted, free of charge, to any person "
         "obtaining a copy\n");
  printf("of this software and associated documentation files (the "
         "\"Software\"), to deal\n");
  printf("in the Software without restriction, including without limitation "
         "the rights\n");
  printf("to use, copy, modify, merge, publish, distribute, sublicense, and/or "
         "sell\n");
  printf("copies of the Software, and to permit persons to whom the Software "
         "is\n");
  printf("furnished to do so, subject to the following conditions:\n\n");

  printf("The above copyright notice and this permission notice shall be "
         "included in\n");
  printf("all copies or substantial portions of the Software.\n\n");

  printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
         "EXPRESS OR\n");
  printf("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
         "MERCHANTABILITY,\n");
  printf("FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT "
         "SHALL THE\n");
  printf("AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR "
         "OTHER\n");
  printf("LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, "
         "ARISING FROM,\n");
  printf("OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
         "DEALINGS IN THE\n");
  printf("SOFTWARE.\n\n");
}
