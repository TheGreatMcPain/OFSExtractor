#include <stdio.h>
#include <stdlib.h>

#include "util.h"

#define MAXPLANES 32 // Most 3D Blurays have 32 planes.

struct OFMDdata {
  int frameRate;
  int totalFrames;
  int numOfPlanes;
};

int getOFMDsFromFile(const char *fileName, BYTE ***OFMDs);
void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes);
int verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes, int validPlanes[],
                 char *inFile);
void parseDepths(int planeNum, int numOfPlanes, BYTE **planes, int numFrames);
void createOFSFiles(BYTE **planes, struct OFMDdata OFMDdata, int validPlanes[],
                    const char *outFolder, BYTE dropFrame);
