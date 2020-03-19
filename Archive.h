#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "LiveFile.h"
#include "RepoInfo.h"
#include "BlockList.h"
#include "Types.h"
#include "BusyLock.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
#include <fstream>
using namespace std;

// returns info from thread to archfilecreate
class HashAndCompressReturn {
    public:

    BusyLock     BL;
    char         CompFlag;
    i64          BlockIdx;
    string       HashHex;
};

// feilds of "List" file
class FileListEntry {
    public:
    string    Name;
    u64       BlkNum;
    eCompType CompType;
    string    Hash;
     FileListEntry() {}
    ~FileListEntry() {}
};

class Archive {
    public:
    RepoInfo     *Repo;
    string        Name;
    string        ArchDirPath;
    string        IDPath;
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

    FileListEntry ParseListLine (const string &ListLine, u64 LineNo);
};

class ArchiveRead : public Archive {
    map <u64, string> InfoBlockIds;
    mutex             InfoBlockIdsMtx;
    map <string, u64> ModTimes;
    mutex             ModTimesMtx;

    void ParseOptions();

    public:

     ArchiveRead (RepoInfo *repo, const string &name);
    ~ArchiveRead ();

    void          DoExtract      ();
    void          DoExtractJob   (const FileListEntry &ListEntry);
};

class ArchiveReference : public ArchiveRead {
    public:
    map <string, FileListEntry> FileMap;

     ArchiveReference (RepoInfo *repo, const string &name);
    ~ArchiveReference ();
};

class ArchiveCreate : public Archive {
    public:
    ArchiveReference *ArchRef;

     ArchiveCreate (RepoInfo *repo, const string &name, ArchiveReference *ref);
    ~ArchiveCreate ();

    void Init         (RepoInfo *repo, const string &name);
    void PushFileList (const FileListEntry &ListEntry);
};

class ArchFile {
    public:
    Archive           *Arch;
    string             Name;
    FileListEntry      ListEntry;
    struct stat        Stats;
    mutex              Mtx;
    string             LinkTarget;
    vector <ChunkInfo> Chunks;

     ArchFile (Archive *arch);
    ~ArchFile ();
};

class ArchFileRead : public ArchFile {
    public:

     ArchFileRead (ArchiveRead *arch, const FileListEntry &ListEntry);
    ~ArchFileRead ();

    void          DoExtract      ();
    void          SlurpFinfo     (i64 Idx, string &FInfoPacked);
};

class ArchFileCreate : public ArchFile {
    public:
    string            Name;
    LiveFile         *LF;
    vector <u64> DataBlkNs;

    ArchFileCreate (ArchiveCreate *arch, LiveFile *lf);

    void Create     (bool Keep);            // add file to archive
    void CreateJob  (bool Keep);            // add file to archive (runs within thread)
    void CreateLink (ArchFileCreate *Prev); // link to previously archived file
    void HashAndCompressJob (string &Chunk, HashAndCompressReturn *HACR);
};

#endif // ARCHIVE_H
