#ifndef BLOCKLIST_H
#define BLOCKLIST_H

#include "Types.h"
#include "RepoInfo.h"
#include "Opts.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <mutex>
#include <fstream>
using namespace std;

class BlockRangeTuple {
    public:
    i64 min, max;
    BlockRangeTuple () {}
    BlockRangeTuple (i64 mn, i64 mx) {
        min = mn;
        max = mx;
    }
};

class BlockList {
    string TopDir;
    const  Opts &O;
    vector <BlockRangeTuple> Ranges;
    mutex  Mtx;

    i64 Search (i64 Idx);
    i64 Search (i64 Idx, i64 Start, i64 End);

    public:
     BlockList (const string &topdir, const Opts &o);
    ~BlockList ();

    i64     Alloc           ();
    void    Free            (i64 Idx);
    void    MarkAllocated   (i64 Idx);
    i64     CountAllocated  ()                              const;
    vecstr  GetSubDirs      (i64 Idx)                       const;
    string  Idx2DirString   (i64 Idx)                       const;
    string  Idx2FileName    (i64 Idx)                       const;
    fstream OpenReadStream  (i64 Idx)                       const;
    void    SlurpBlock      (i64 Idx,       string &BufStr) const;
    void    SpitBlock       (i64 Idx, const string &BufStr);
    i64     SpitNewBlock    (         const string &BufStr);
    void    Link            (i64 Idx, const string &Target);
    void    Clone           (BlockList &Ref);
    void    ReverseAlloc    ();
    void    ReverseAlloc    (const string &Dir);
    void    UnLink          (i64 Idx);
};

#endif // BLOCKLIST_H
