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

// this should be something that's very unlikely to show up in a file name or soft link target
static const char* ListRecSep = " \\\'\'\\ ";  /* \''\ */

// returns info from thread to archfilecreate
class HashAndCompressReturn {
    public:

    BusyLock     BL;
    char         CompFlag;
    i64          BlockIdx;
    string       Hash;
    bool         Keep;

    HashAndCompressReturn () : BL (true) {}
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
    map <i64, HLinkSyncRec*> HLinkSyncs;
    mutex                    HLinkSyncsMtx;
    vector <DirAttribRec>    DirAttribs;
    mutex                    DirAttribsMtx;

    void ParseOptions();

    public:

     ArchiveRead (RepoInfo *repo, const string &name);
    ~ArchiveRead ();

    void DoExtract    ();
    void DoExtractJob (const string &ListLine, u64 LineNo);
};

class ArchiveBase : public ArchiveRead {
    public:
    map <string, FileListEntry> FileMap;
    mutex                       FileMapMtx;

     ArchiveBase (RepoInfo *repo, const string &name);
    ~ArchiveBase ();
};

class ArchiveCreate : public Archive {
    public:
    i64          ZeroLenIdx;
    mutex        ZeroLenIdxMtx;
    ArchiveBase *ArchBase;

     ArchiveCreate (RepoInfo *repo, const string &name, ArchiveBase *base);
    ~ArchiveCreate ();

    void Init                 (RepoInfo *repo, const string &name);
    void PushListEntry        (const FileListEntry &ListEntry);
    void PurgeUnusedBlocksJob (const FileListEntry *ListEntry);
    void PurgeUnusedBlocks    ();
};

class ArchFile {
    public:
    Archive           *Arch;
    string             Name;
    FileListEntry      ListEntry;
    mutex              Mtx;
    string             LinkTarget;
    vector <ChunkInfo> Chunks;

     ArchFile (Archive *arch);
    ~ArchFile ();
};

class ArchFileRead : public ArchFile {
    public:
    ArchiveRead    *Arch;

     ArchFileRead (ArchiveRead *arch, const FileListEntry &ListEntry);
    ~ArchFileRead ();

    void DoExtract  ();
};

class ArchFileCreate : public ArchFile {
    public:
    ArchiveCreate *Arch;
    string         Name;
    LiveFile      *LF;

     ArchFileCreate (ArchiveCreate *arch, LiveFile *lf);
    ~ArchFileCreate ();

    void Create     (bool KeepAF);          // add file to archive
    void CreateJob  (bool KeepAF);          // add file to archive (runs within thread)
    void CreateLink (ArchFileCreate *Prev); // link to previously archived file
    void HashAndCompressJob (const string &ChunkData
                            ,const ChunkInfo *BaseChunkInfo, BlockList *BaseBlockList
                            ,HashAndCompressReturn *HACR);
};

#endif // ARCHIVE_H
