#ifndef ARCHIVEREAD_H
#define ARCHIVEREAD_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "BlockList.h"
#include "Opts.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
using namespace std;

class ArchiveRead {
    public:
    string        Name;
    string        ArchDirPath;
    RepoInfo     *Repo;
    FILE         *ListFile;
    BlockList     FInfoBlocks;
    BlockList     ChunkBlocks;

     ArchiveRead () {} // blank constructor
    ~ArchiveRead () {} // blank destructor
    void Init (RepoInfo *repo, Opts &o);
    void DoExtract();
    void ParseOptions();
};

class ArchFileRead {
    public:
    string            Name;
    ArchiveRead      *ArchRead;
    BlockIdxType      InfoBlkNum;
    char              InfoBlkComp;
    string            InfoBlkHash;
    mutex             Mtx;
    string            Stats;
    vector <uint64_t> DataBlkNs;

    ArchFileRead () {} // blank constructor
    ArchFileRead (ArchiveRead *arch);
};

#endif // ARCHIVEREAD_H
