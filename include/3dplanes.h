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
#pragma once
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

#define MAXPLANES 32 // Most 3D Blurays have 32 planes.

struct OFMDdata {
  int frameRate;
  int totalFrames;
  int numOfPlanes;
};

int getOFMDsInFile(size_t storeSize, size_t bufferSize, const char *filename,
                   BYTE ***OFMDs);

void getPlanesFromOFMDs(BYTE ***OFMDs, int numOFMDs, struct OFMDdata *OFMDdata,
                        BYTE ***planes);

int verifyPlanes(struct OFMDdata OFMDdata, BYTE **planes, int validPlanes[],
                 char *inFile);

void parseDepths(int planeNum, int numOfPlanes, BYTE **planes, int numFrames);

void createOFSFiles(BYTE **planes, struct OFMDdata OFMDdata, int validPlanes[],
                    const char *outFolder, BYTE dropFrame);
