#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "BlockList.h"
#include "Opts.h"
#include "Types.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
using namespace std;

class Archive {
    public:
    RepoInfo     *Repo;
    string        Name;
    string        ArchDirPath;
    string        ListPath;
    string        LogPath;
    string        OptionsPath;
    string        FinfoDirPath;
    string        ChunkDirPath;
    string        ExtraDirPath;
    fstream       LogFile;
    fstream       ListFile;
    BlockList    *FInfoBlocks;
    BlockList    *ChunkBlocks;

     Archive (RepoInfo *repo, const string &name);
    ~Archive ();
};

class ArchiveRead : public Archive {
    Opts                   O;
    map <uint64_t, string> InfoBlockIds;
    mutex                  InfoBlockIdsMtx;
    map <string, uint64_t> ModTimes;
    mutex                  ModTimesMtx;

    void ParseOptions();

    public:
     ArchiveRead (RepoInfo *repo, const string &name, Opts &o);
    ~ArchiveRead ();
    void DoExtract();
    void DoExtractJob (const string &FileLine, uint64_t LineNo);
};

class ArchiveCreate : public Archive {
    public:

     ArchiveCreate (RepoInfo *repo, const string &name);
    ~ArchiveCreate ();

    void Init         (RepoInfo *repo, const string &name);
    void PushFileList (const string &Fname, BlockIdxType Block, char Comp, const string &Hash);
};

class ArchFile {
    public:
    Archive           *Arch;
    string             Name;
    BlockIdxType       InfoBlkNum;
    char               InfoBlkComp;
    string             InfoBlkHash;
    mutex              Mtx;
    string             Stats;
    string             LinkTarget;
    vector <ChunkInfo> Chunks;

     ArchFile (Archive *arch);
    ~ArchFile ();
};

class ArchFileRead : public ArchFile {
    public:

     ArchFileRead (ArchiveRead *arch, const string &ListEntry, uint64_t LineNo);
    ~ArchFileRead ();

    void DoExtract  ();
    void SlurpFinfo (BlockIdxType Idx, string &FInfoPacked);
};

class ArchFileCreate : public ArchFile {
    public:
    string            Name;
    LiveFile         *LF;
    vector <uint64_t> DataBlkNs;

    ArchFileCreate (ArchiveCreate *arch, LiveFile *lf);

    void Create     (bool Keep);            // add file to archive
    void CreateJob  (bool Keep);            // add file to archive (runs within thread)
    void CreateLink (ArchFileCreate *Prev); // link to previously archived file
};

#endif // ARCHIVE_H
