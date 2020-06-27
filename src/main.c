#define _FILE_OFFSET_BITS 64
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dplanes.h"
#include "util.h"

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
void usage(int argc, char *argv[]);
void printIntro(int argc, char *argv[]);
char *printFpsValue(int frameRate);

int main(int argc, char *argv[]) {
  BYTE **OFMDs;
  BYTE **planes;
  BYTE newFrameRate = 0;
  BYTE dropFrame = 0;
  struct OFMDdata OFMDdata;
  int validPlanes[MAXPLANES]; // Most BluRays have 32 planes.
  int planesInFile;
  int planesWritten;
  int numOFMDs;
  int numOfValidPlanes;
  char *outFolder;

  // Prevent creating false ofs files.
  for (int x = 0; x < MAXPLANES; x++) {
    validPlanes[x] = 0;
  }

  printIntro(argc, argv);
  parseOptions(argc, argv, &newFrameRate, &dropFrame, &outFolder);

  if (newFrameRate >= 0) {
    OFMDdata.frameRate = newFrameRate;
  }

  printf("Searching file for 3D-Planes.\n\n");

  OFMDs = (BYTE **)malloc(sizeof(BYTE *));
  numOFMDs = getOFMDsFromFile(argv[1], &OFMDs);

  if (numOFMDs == 0) {
    printf("This file doesn't have any 3D-Planes.\n");
    exit(1);
  }

  getPlanesFromOFMDs(&OFMDs, numOFMDs, &OFMDdata, &planes);

  printf("\nChecking 3D-Planes for valid depth values.\n");

  planesInFile = verifyPlanes(OFMDdata, planes, validPlanes, argv[1]);
  createOFSFiles(planes, OFMDdata, validPlanes, outFolder, dropFrame);

  for (int x = 0; x < MAXPLANES; x++) {
    if (validPlanes[x] == 1) {
      numOfValidPlanes++;
    }
  }

  // Don't leak memory!
  free2DArray((void ***)&planes, OFMDdata.numOfPlanes);
  free2DArray((void ***)&OFMDs, numOFMDs);

  printf("\nNumber of 3D-Planes in MVC stream: %d\n", planesInFile);
  printf("Number of 3D-Planes written: %d\n", numOfValidPlanes);
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

void parseOptions(int argc, char *argv[], BYTE *newFrameRate, BYTE *dropFrame,
                  char **outFolder) {
  char *supportedExt[3] = {"mvc", "h264", "m2ts"};
  char *supportedExtUpper[3] = {"MVC", "H264", "M2TS"};
  const char *fileExt;
  bool validExt;

  if (argc >= 2) {
    if (strncmp(argv[1], "-license", 8) == 0) {
      // TODO: print license
      printf("MIT\n");
      exit(0);
    }
  }

  if (argc >= 2) {
    fileExt = getFileExt(argv[1]);
    validExt = false;
    for (int x = 0; x < 3; x++) {
      if (strcmp(supportedExt[x], fileExt) == 0 ||
          strcmp(supportedExtUpper[x], fileExt) == 0) {
        validExt = true;
      }
    }
    if (!validExt) {
      printf("'%s': Is not a supported file extention.\n", fileExt);
      exit(1);
    }
  }

  if (argc == 1) {
    usage(argc, argv);
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

void printIntro(int argc, char *argv[]) {
  char *program = basename(argv[0]);

  // Print version info, and 'arch'.
  printf("OFSExtractor %s %sby TheGreatMcPain (aka Sixsupersonic on doom9)\n",
         VERSION, ARCH);
#ifndef DATE // Set compile date. (Might change this to git commit date.)
  printf("This program was compiled on %s %s.\n\n", __DATE__, __TIME__);
#else
  printf("This program was compiled on %s.\n\n", DATE);
#endif
}

void usage(int argc, char *argv[]) {
  char *program = basename(argv[0]);

  printf("Usage: %s [-license] <input file> <output folder> [-fps # "
         "-dropframe] \n\n",
         program);
  printf("  -license : Print license (MIT).\n\n");
  printf("  <input file> : Can be a raw MVC file, ");
  printf("a H264+MVC combined from MakeMKV,\n");
  printf("                 or an M2TS. M2TS is not supported, and could cause "
         "burnt toast.)\n\n");
  printf("  <output folder> : The output folder which will the ofs files.\n");
  printf("                    If empty the current directory will be used\n\n");
  printf("Advanced Options: Use with care!\n\n");
  printf("  -fps # : Must be value between 1 and 4, 6, or 7. See Table.\n");
  printf("           This will override the fps value that would normally come "
         "from the MVC stream.\n");
  printf("           See conversion table below.\n\n");
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