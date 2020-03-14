#include "BlockList.h"
#include "Logging.h"
#include "Opts.h"
#include "Utils.h"

#include <filesystem>
#include <string>
#include <sstream>
#include <stdio.h>
namespace fs = std::filesystem;

BlockList::BlockList (const string &topdir, const Opts &o) {
    TopDir = topdir;
    O = o;
}

BlockList::~BlockList () {
}

// allocate a block index
BlockIdxType BlockList::Alloc () {
    Mtx.lock();

    BlockIdxType Idx;

    if (Ranges.size() == 0) {
        // create first allocated range
        BlockRangeTuple R (0,0);
        Ranges.push_back (R);
        Idx = 0;
    } else {
        // allocate from the first range
        BlockRangeTuple R0 = Ranges[0];
        Idx = ++R0.max;

        // see if we need to merge with the next range
        if (Ranges.size() > 1) {
            BlockRangeTuple R1 = Ranges[1];
            if (Idx > R1.min)
                THROW_PBEXCEPTION ("Block Allocation list corrupted. Idx:%d NextMin:%d", Idx, R1.min);
            if (Idx == R1.min) {
                // merge ranges 0 and 1
                R0.max = R1.max;
                Ranges.erase (Ranges.begin()+1);
            }
        }
        Ranges[0] = R0;
    }

    Mtx.unlock();
    return Idx;
}

int BlockList::Search (BlockIdxType Idx) {
    return Search (Idx, 0, Ranges.size()-1);
}

// find a block index within the allocated blocks
int BlockList::Search (BlockIdxType Idx, int Start, int End) {
    if (Start > End)
        return -1;

    // Note: binary search

    // check the middle of the search range
    int Mid = (Start + End) / 2;
    BlockRangeTuple RMid = Ranges [Mid];
    if (Idx >= RMid.min && Idx <= RMid.max)
        return Mid;

    // check lower range
    if (Idx < RMid.min)
        return Search (Idx, Start, Mid-1);

    // check upper range
    return Search (Idx, Mid+1, End);
}

// free a block index from the allocated blocks
void BlockList::Free (BlockIdxType Idx) {
    Mtx.lock();

    // find the range containing the block index
    int RangeIdx = Search (Idx);
    if (RangeIdx < 0)
        THROW_PBEXCEPTION ("BlockList::Free: Attempt to free unallocated index: %d", Idx);
    BlockRangeTuple Range = Ranges[RangeIdx];

    if (Range.min == Idx) {
        // free from low end of range
        Range.min++;
    } else if (Range.max == Idx) {
        // free from high end of range
        Range.max--;
    } else {
        // free from inside range

        // split the range into two ranges
        BlockRangeTuple RNext (Idx+1, Range.max);
        Ranges.insert (Ranges.begin()+RangeIdx+1, RNext);

        Range.max = Idx-1;
    }

    // update the current range
    if (Range.min > Range.max)
        Ranges.erase(Ranges.begin()+RangeIdx);
    else
        Ranges[RangeIdx] = Range;

    Mtx.unlock();
}

// convert a block number to a list of directory components
vector <string> BlockList::GetSubDirs (BlockIdxType Idx) {
    vector <string> SubDirs;
    BlockIdxType TmpIdx = Idx / O.BlockNumModulus;
    while (TmpIdx) {
        unsigned Part   = TmpIdx % O.BlockNumModulus;
                 TmpIdx = TmpIdx / O.BlockNumModulus;

        stringstream SubDir;
        SubDir << "d" << setfill('0') << setw (O.BlockNumDigits) << Part;
        SubDirs.push_back (SubDir.str());
    }
    return SubDirs;
}

// convert a block number to a directory path
string BlockList::Idx2DirString (BlockIdxType Idx) {
    vector <string> Dirs = GetSubDirs (Idx);
    Dirs.insert (Dirs.begin(), 1, TopDir);
    return Utils::JoinStrs (Dirs, "/");
}

// convert a block number to a path relative to a top dir
string BlockList::Idx2FileName (BlockIdxType Idx) {
    return Idx2DirString (Idx) + "/" + to_string (Idx);
}

FILE *BlockList::OpenBlockFile (BlockIdxType Idx, const char *mode) {
    // create subdirs
    string SubDirName = Idx2DirString(Idx);
    Utils::CreateDir (SubDirName, true);

    string BlockFileName = SubDirName + "/" + to_string (Idx);
    FILE *BlkFile = fopen (BlockFileName.c_str(), mode);
    if (!BlkFile)
        THROW_PBEXCEPTION_IO ("Can't open block file: " + BlockFileName);

    return BlkFile;
}

void BlockList::SlurpBlock (BlockIdxType Idx, string &BufStr) {
    FILE *F = Utils::OpenReadBin (Idx2FileName (Idx));

    unsigned TotalSize = 0;

    int BytesRead;
    do {
        BufStr.resize (TotalSize + O.ChunkSize);
        BytesRead = Utils::ReadBinary (F, BufStr.data() + TotalSize, O.ChunkSize);
        TotalSize += BytesRead;
    } while (BytesRead);

    BufStr.resize (TotalSize);

    fclose (F);
}
