#ifndef BLOCKLIST_H
#define BLOCKLIST_H

#include "LiveFile.h"
#include "RepoInfo.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
using namespace std;

typedef uint64_t BlockIdxType;
class BlockRangeTuple {
    public:
    BlockIdxType min, max;
    BlockRangeTuple (BlockIdxType mn, BlockIdxType mx) {
        min = mn;
        max = mx;
    }
};

class BlockList {
    string TopDir;
    vector <BlockRangeTuple> Ranges;
    mutex  Mtx;
    int Search (BlockIdxType Idx);
    int Search (BlockIdxType Idx, int Start, int End);

    public:
    BlockList () {}
    BlockList (string topdir) {
        Init (topdir);
    }
    ~BlockList () {
    }
    void Init (string topdir) {
        TopDir = topdir;
    }

    BlockIdxType Alloc ();
    void Free (BlockIdxType Idx);
    vector <string> GetSubDirs (BlockIdxType Idx);
    string Idx2DirString (BlockIdxType Idx);
    string Idx2FileName (BlockIdxType Idx);
    FILE *OpenBlockFile (BlockIdxType Idx, const char *mode);
};

#endif // BLOCKLIST_H
