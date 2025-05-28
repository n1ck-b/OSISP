#ifndef COURSEWORK_UTIL_FUNCTIONS_H
#define COURSEWORK_UTIL_FUNCTIONS_H

#include "header.h"
#include "direntry.h"
#include "fsinfo.h"
#include "fs_state.h"
#include "fat.h"

DirEntry* readAllEntries();
int splitPath(char* path, char*** components, int* depth);
int findDir(char* dirName, DirEntry* entries, int* index, int numOfDirs);
unsigned char* readCluster(unsigned numOfCluster);
unsigned char* readClusters(unsigned short numOfFirstCluster, int* numOfReadClusters);
DirEntry* convertToDirEntries(unsigned char* buffer, int numOfEntries, int numOfClusters);
unsigned short findLastCluster(unsigned short numOfFirstCluster);
unsigned short checkClusterForEmptySlots(unsigned char* buffer);
int allocateNewCluster(int numOfLatestCluster, int isDir);
unsigned checkForExistenceOfFileOrDirectory(DirEntry* entries, unsigned short numOfEntries, char* entryName, unsigned char attr);
int writeDirEntriesToCluster(unsigned short numOfCluster, DirEntry* entries);
int writeDirEntriesToEmptyCluster(unsigned short clusterNum, DirEntry* entries);
unsigned countSubDirs(DirEntry* entries, unsigned short numOfEntries);
struct timespec convertTimeToTimespec(time_t time);
unsigned short countClustersInChain(unsigned short startCluster);
int writeBytesToFile(unsigned char startCluster, unsigned char* buffer);
int rewriteDirEntries(unsigned short startCluster, DirEntry* entries, unsigned short numOfEntries);
int rewriteRootDirEntries(DirEntry* entries, unsigned short numOfEntries);
void freeClustersInChainExceptFirst(unsigned short firstCluster);
unsigned short countFreeBytesInCluster(unsigned char* cluster);
unsigned short* getClustersFromChain(unsigned short firstCluster, int* counter);
int freeCluster(unsigned short numOfCluster);
int zeroBytes(unsigned short numOfCluster, unsigned short numOfBytesToZero);
time_t convertTimeFromTimespec(struct timespec tv);
unsigned countDirSize(unsigned short startCluster);
void freeAllClustersInChain(unsigned short firstCluster);
int freeClusterIfNeeded(int index, unsigned short startClusterOfParentDir);
int saveFatToFile();
int saveFsInfoToFile();

#endif //COURSEWORK_UTIL_FUNCTIONS_H
