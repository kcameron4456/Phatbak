#ifndef BLOCKLIST_H
#define BLOCKLIST_H

#include "RepoInfo.h"
#include "Opts.h"
#include "Types.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
#include <fstream>
using namespace std;

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
    Opts   O;
    vector <BlockRangeTuple> Ranges;
    mutex  Mtx;

    int Search (BlockIdxType Idx);
    int Search (BlockIdxType Idx, int Start, int End);

    public:
     BlockList (const string &topdir, const Opts &o);
    ~BlockList ();

    BlockIdxType    Alloc           ();
    void            Free            (BlockIdxType Idx);
    vector <string> GetSubDirs      (BlockIdxType Idx);
    string          Idx2DirString   (BlockIdxType Idx);
    string          Idx2FileName    (BlockIdxType Idx);
    fstream         OpenReadStream  (BlockIdxType Idx);
    void            SlurpBlock      (BlockIdxType Idx,       string &BufStr);
    void            SpitBlock       (BlockIdxType Idx, const string &BufStr);
    BlockIdxType    SpitNewBlock    (                  const string &BufStr);
};

#endif // BLOCKLIST_H
