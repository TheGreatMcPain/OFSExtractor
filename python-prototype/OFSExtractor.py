#!/usr/bin/env python3
import sys
import os


def main(args):
    fileName = os.path.expanduser(args[0])
    OFMDs = getOFMDsFromFile(fileName)
    OFMDdata = getPlanesFromOFMDs(OFMDs)
    verifyPlanes(OFMDdata)


def getOFMDsFromFile(fileName):
    """
    Takes in a raw MVC/H264 file, collects the OFMDs from it
    to a list.
    """
    blockSize = (1024 * 1024) * 1
    OFMDs = []

    with open(fileName, 'rb') as f:
        while True:
            keepgoing = True
            piece = f.read(blockSize)
            posOFMD = piece.find(b"OFMD")
            while posOFMD == -1:
                piece += f.read(blockSize)
                posOFMD = piece.find(b"OFMD")
                # Once we reach the end of the file stop the
                # whole nested loop.
                if os.stat(fileName).st_size == f.tell():
                    keepgoing = False
                    break
            if not keepgoing:
                break

            # Seek file to the OFMD's location
            f.seek(-len(piece), 1)
            f.seek(posOFMD, 1)

            piece = f.read(blockSize)
            # Lazy varify if the OFMD is valid by checking it's
            # framerate value.
            frameRate = piece[4] & 15
            if frameRate in [1, 2, 3, 4, 5, 6, 7]:
                # Place 4K of bytes into the OFMDs list.
                OFMDs.append(piece[0:4096])

            # Print the status in percent.
            percent = int((f.tell() / os.stat(fileName).st_size) * 100)
            progress = "Progress: " + str(percent) + "%"
            print(f'{progress}\r', end='')

            # Seek file to the OFMD's location plus 1 byte.
            f.seek(-len(piece), 1)
            f.seek(1, 1)
    print()
    return OFMDs


def getPlanesFromOFMDs(OFMDs):
    """
    Parse data from an OFMDs.
    """
    def getNumOfPlanes(OFMD):
        hexNum = OFMD[10]
        binary = bin(hexNum)
        result = binary[-7:]
        return int(result, 2)

    planes = []

    # Lets hope the frame-rate, and the number of planes don't change
    frameRate = OFMDs[0][4] & 15
    numOfPlanes = getNumOfPlanes(OFMDs[0])

    # Allocate planes list
    counter = 0
    while counter < numOfPlanes:
        planes.append([])
        counter += 1

    # Put the depth values into their respective plane.
    for ofmd in OFMDs:
        frameCount = ofmd[11] & 127
        for plane in planes:
            planeNum = planes.index(plane)
            for x in range(14 + (planeNum * frameCount),
                           14 + (planeNum * frameCount) + frameCount):
                plane.append(ofmd[x])

    OFMDData = {}
    OFMDData['frameRate'] = frameRate
    OFMDData['planes'] = planes
    return OFMDData


def verifyPlanes(OFMDData):
    frameRate = OFMDData['frameRate']
    if frameRate == 1:
        fps = 23.976
    elif frameRate == 2:
        fps = 24
    elif frameRate == 3:
        fps = 25
    elif frameRate == 4:
        fps = 29.97
    elif frameRate == 6:
        fps = 50
    elif frameRate == 7:
        fps = 60

    planes = OFMDData['planes']

    for plane in planes:
        thereArePlanes = False
        planeIndex = planes.index(plane)
        for x in plane:
            if x != 0x80:
                thereArePlanes = True

        if thereArePlanes:
            print()
            print("3D-Plane #" + str(planeIndex))
            parseDepths(plane, fps, len(plane))
        else:
            print()
            print("3D-Plane #" + str(planeIndex) + " is Empty!")
            planes[planeIndex] = []


def parseDepths(data, framerate, numframes):
    minval = 128
    maxval = -128
    total = 0
    undefined = 0
    firstframe = -1
    lastframe = -1
    lastval = ""
    cuts = 0

    for i in range(0, numframes):
        byte = data[i]
        if byte != lastval:
            cuts += 1
            lastval = byte

        if byte == 128:
            undefined += 1
            continue
        else:
            lastframe = i
            if firstframe == -1:
                firstframe = i

        # If the value is bigger than 128.
        # Negate the value.
        if byte > 128:
            byte = 128 - byte

        if byte < minval:
            minval = byte
        if byte > maxval:
            maxval = byte

        total += byte

    print("NumFrames:", numframes)
    print("Minimum depth:", minval)
    print("Maximum depth:", maxval)
    print(
        "Average depth:",
        round((float(total) / (float(numframes) - float(undefined))) * 100.0) /
        100.0)
    print("Number of changes of depth value:", cuts)
    print("First frame with a defined depth:", firstframe)
    print("Last frame with a defined depth:", lastframe)
    print("Undefined:", undefined)
    if minval == maxval:
        print("*** Warning This 3D-Plane has a fixed depth of minval! ***")


if __name__ == "__main__":
    main(sys.argv[1:])
