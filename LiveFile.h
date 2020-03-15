#ifndef LIVEFILE_H
#define LIVEFILE_H

#include "Types.h"
#include "BlockList.h"

#include <sys/stat.h>
#include <string>
#include <vector>
#include <map>
using namespace std;

class LiveFile {
    public:
    string      Name;       // Full pathname for the file
    FILE       *F;          // File for i/o
    struct stat Stats;      // File status info from lstat() call
    string      LinkTarget; // Target of soft link

    // for create
    LiveFile  (const string &name);

    // for extract, etc
    LiveFile  (const string &name        , const string &stats   , const string &ltarg
              ,vector <ChunkInfo> &Chunks, BlockList *ChunkBlocks
              ,map <string, uint64_t> &ModTimes, mutex *ModTimesMtx
              ,bool DoHLink
              );

    ~LiveFile ();

    // helper functions
    inline bool IsFile   () const {return S_ISREG  (Stats.st_mode);}
    inline bool IsDir    () const {return S_ISDIR  (Stats.st_mode);}
    inline bool IsSLink  () const {return S_ISLNK  (Stats.st_mode);}
    inline bool IsFifo   () const {return S_ISFIFO (Stats.st_mode);}
    inline bool IsSocket () const {return S_ISSOCK (Stats.st_mode);}

    inline int64_t mTime()  const {return (int64_t) Stats.st_mtime * 1000000000 +
                                          (int64_t) Stats.st_mtim.tv_nsec;}
    inline uint16_t Dev  () const {return Stats.st_dev;}
    inline uint16_t Mode () const {return Stats.st_mode;}
    inline uint64_t Size () const {return Stats.st_size;}
    inline uint64_t INode() const {return (IsDir() || IsSLink() || Stats.st_nlink < 2) ? 0 : Stats.st_ino;} // only for non-dir hlink files
    inline void     Trunc()       {Stats.st_size = 0;}

    vector <string> GetSubs ();

    string MakeInfoHeader                    () const ;
    void   ImportInfoHeader (const string &Hdr);

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
