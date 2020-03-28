#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
using namespace std;

// get easier names for commonly used types
// TBD: use these everywhere
typedef int8_t          i8;
typedef int16_t         i16;
typedef int32_t         i32;
typedef int64_t         i64;
typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef uint64_t        u64;
typedef vector <string> vecstr;

// these are mostly put here to break circular header dependencies

typedef enum {
    CompType_NONE = 0,
    CompType_ZTSD,
    CompType_NULL,  // marks end of list
} eCompType;

#include "Hash.h"
#include "Comp.h"
#include "BusyLock.h"

#include <sys/stat.h>

// fields of "List" file
class FileListEntry {
    public:
    string      Name      ;
    struct stat Stats     ;
    char        CompFlag  ;
    string      LinkTarget;
    i64         FInfoIdx  ;
    u64         LineNo    ;
    string      Acl       ;

     FileListEntry() {}
    ~FileListEntry() {}
};

class ChunkInfo {
    public:
    char         CompFlag;
    i64          ChunkIdx;
    string       Hash;
    ChunkInfo (char compflag, i64 idx, const string& hash) {
        CompFlag = compflag;
        ChunkIdx = idx;
        Hash     = hash;
    }
};

// needed to synchonize file extract with hard link creation
class HLinkSyncRec {
    public:
    string   Name;
    BusyLock Lock;
    HLinkSyncRec () : Lock (true) {}
};

// holds information for deferred setting of permissions and mod time on extracted dirs
class DirAttribRec {
    public:
    string   Name;
    u32      Uid;
    u32      Gid;
    mode_t   Mode;
    u64      MTime;
    string   Acl;
};

// holds information for each inode to allow
// possible hard-link processing during archive creation
struct InodeInfo {
    FileListEntry ListEntry; // first file list entry for the inode
    bool          Complete;  // true when the first file has been archived and ListEntry is valid
    vecstr        Links;     // list of additional names wainting to be linked
    mutex         Mtx;       // synchronizes access to Complete and Links
};

#endif // TYPES_H
