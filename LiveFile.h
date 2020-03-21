#ifndef LIVEFILE_H
#define LIVEFILE_H

#include "Types.h"
#include "BlockList.h"
#include "BusyLock.h"

#include <string>
#include <vector>
#include <map>
using namespace std;

void ExtractChunkJob (const ChunkInfo *Chunk, const BlockList *ChunkBlocks, i64 BlockIdx, FILE *F, BusyLock *Lock, BusyLock *PrevLock);

class LiveFile {
    public:
    string      Name;       // Full pathname for the file
    FILE       *F;          // File for i/o
    struct stat Stats;      // File status info from lstat() call
    string      LinkTarget; // Target of soft link

    // for create
    LiveFile  (const string &name);

    // for extract, etc
    LiveFile  (const FileListEntry &ListEntry
              ,const vector <ChunkInfo> &Chunks , const BlockList *ChunkBlocks
              ,vector <DirAttribRec> &DirAttribs, mutex *DirAttribsMtx
              ,bool DoHLink
              );

    ~LiveFile ();

    // helper functions
    inline bool IsFile   () const {return S_ISREG  (Stats.st_mode);}
    inline bool IsDir    () const {return S_ISDIR  (Stats.st_mode);}
    inline bool IsSLink  () const {return S_ISLNK  (Stats.st_mode);}
    inline bool IsFifo   () const {return S_ISFIFO (Stats.st_mode);}
    inline bool IsSocket () const {return S_ISSOCK (Stats.st_mode);}

    inline uint16_t Dev  () const {return Stats.st_dev;}
    inline uint16_t Mode () const {return Stats.st_mode;}
    inline u64      Size () const {return Stats.st_size;}
    inline u64      INode() const {return (IsDir() || IsSLink() || Stats.st_nlink < 2) ? 0 : Stats.st_ino;} // only for non-dir hlink files
    inline void     Trunc()       {Stats.st_size = 0;}

    vecstr GetSubs ();

    void     OpenRead  ();
    void     OpenWrite ();
    void     Close     ();
    int      Read      (char         *Buf, int ReqSize);
    int      ReadChunk (char       *Chunk);
    int      ReadChunk (string     &Chunk);
    void     Write     (const string &Str);
    void     Write     (char         *Buf, int BufSize);
};

void SplitFileName (const string &RawName, string &DirPart, string &FilePart);

#endif // LIVEFILE_H
