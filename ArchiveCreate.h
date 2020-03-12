#ifndef ARCHIVECREATE_H
#define ARCHIVECREATE_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "BlockList.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
using namespace std;

class ArchiveCreate {
    public:
    string         Name;
    string         ArchDirName;
    RepoInfo      *Repo;
    FILE          *LogFile;
    FILE          *ListFile;
    BlockList      FInfoBlocks;
    BlockList      ChunkBlocks;

    ArchiveCreate () {} // blank constructor
    ~ArchiveCreate ();
    ArchiveCreate (RepoInfo *repo, const string &name);
    void Init (RepoInfo *repo, const string &name);
    void PushFileList (const string &Fname, BlockIdxType Block, char Comp, const string &Hash);
};

class ArchFileCreate {
    public:
    string            Name;
    ArchiveCreate    *ArchCreate;
    BlockIdxType      InfoBlkNum;
    char              InfoBlkComp;
    string            InfoBlkHash;
    mutex             Mtx;
    string            Stats;
    vector <uint64_t> DataBlkNs;

    ArchFileCreate () {} // blank constructor
    ArchFileCreate (ArchiveCreate *arch);

    void Create     (LiveFile &lf);                       // add file to archive
    void CreateLink (LiveFile &lf, ArchFileCreate *Prev); // link to previously archived file
};

#endif // ARCHIVECREATE_H
