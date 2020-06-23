#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void getOFMDsFromFile(char *fileName, char **OFMDs);
void *my_memmem(const void *big, size_t big_len, const void *little,
                size_t little_len);

int main(int argc, char *argv[]) {
  char **OFMDs;
  getOFMDsFromFile(argv[1], OFMDs);

  return 0;
}

void getOFMDsFromFile(char fileName[], char **OFMDs) {
  size_t blockSize = (1024 * 1024) * 1;
  size_t posOFMD;
  int keepgoing;
  size_t mvcFileSize;
  char *OFMDptr;
  char *ofmd;
  char *buffer;
  FILE *mvcFile;
  int frameCount;
  size_t counter = 0;

  mvcFile = fopen(fileName, "rb");
  fseek(mvcFile, 0L, SEEK_END);
  mvcFileSize = ftell(mvcFile);
  fseek(mvcFile, 0L, SEEK_SET);

  while (1) {
    keepgoing = 1;
    buffer = (char *)malloc(blockSize * sizeof(char));
    size_t freadValue = fread(buffer, blockSize, 1, mvcFile);
    size_t nbytes = blockSize;

    if (freadValue == 0) {
      keepgoing = 0;
    }

    if ((OFMDptr = my_memmem(buffer, blockSize, "OFMD", 4)) != NULL) {
      posOFMD = OFMDptr - buffer;
    } else {
      posOFMD = -1;
    }

    // Until we find new posOFMD. Search more of the file.
    while (posOFMD == -1) {
      buffer = (char *)realloc(buffer, (blockSize + nbytes) * sizeof(char));
      size_t xbytes = fread(buffer + nbytes, blockSize, 1, mvcFile);
      nbytes += blockSize;
      if ((OFMDptr = my_memmem(buffer, nbytes, "OFMD", 4)) != NULL) {
        posOFMD = OFMDptr - buffer;
      } else {
        posOFMD = -1;
      }

      // TODO: When we reach the end of the file stop the loops.
      if (xbytes <= 0) {
        keepgoing = 0;
      }
    }

    // Seek file to the OFMD's location
    fseek(mvcFile, -(nbytes), SEEK_CUR);
    fseek(mvcFile, posOFMD, SEEK_CUR);

    // Load a chunk of the file starting at the position of the OFMD.
    buffer = (char *)realloc(buffer, blockSize * sizeof(char));
    fread(buffer, blockSize, 1, mvcFile);

    // Process Data. (Right now it's just printing the total framecount.
    // When I'm satisfied I'll try to put this data into the OFMDs array.)
    char frameRate = buffer[4] & 15;

    if (frameRate == 1) {
      counter += 1;
      frameCount += buffer[11] & 127;
      // printf("Counter %d\n", counter);
      printf("FrameCount: %d\n", frameCount);
    }

    // Seek file to the OFMD's location plus 1 byte.
    fseek(mvcFile, -(blockSize), SEEK_CUR);
    fseek(mvcFile, 1, SEEK_CUR);
    free(buffer);

    if (keepgoing == 0) {
      break;
    }
  }
}

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
