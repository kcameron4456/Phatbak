#ifndef ARCHIVE_H
#define ARCHIVE_H

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

class Archive {
    public:
    string    Name;
    string    ArchDirName;
    RepoInfo *Repo;
    FILE     *LogFile;
    BlockList FInfoBlocks;
    BlockList ChunkBlocks;

    Archive () {} // blank constructor
    Archive (RepoInfo *repo, const string &name);
    void Init (RepoInfo *repo, const string &name);
};

class ArchFile {
    public:
    string   Name;
    Archive *Arch;
    uint64_t InfoBlkN;
    string   Stats;
    vector <uint64_t> DataBlkNs;

    ArchFile () {} // blank constructor
    ArchFile (Archive *arch, const LiveFile &lf);

    void Create ();  // add file to archive
};

#endif // ARCHIVE_H
