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
i64 BlockList::Alloc () {
    unique_lock<mutex> lock(Mtx);

    i64 Idx;

    if (Ranges.size() == 0) {
        // create first allocated range
        Ranges.emplace_back (0,0);
        Idx = 0;
    } else {
        // allocate from the first range
        BlockRangeTuple &Range = Ranges[0];
        Idx = ++Range.max;

        // see if we need to merge with the next range
        if (Ranges.size() > 1) {
            BlockRangeTuple &R1 = Ranges[1];
            if (Idx > R1.min)
                THROW_PBEXCEPTION ("Block Allocation list corrupted. Idx:" PRIi64 " NextMin:" PRIi64, Idx, R1.min);
            if (Idx == R1.min) {
                // merge ranges 0 and 1
                Range.max = R1.max;
                Ranges.erase (Ranges.begin()+1);
            }
        }
    }

    return Idx;
}

// find the last tuple whose max value is less than or equal to a given index
i64 BlockList::Search (i64 Idx) {
    return Search (Idx, 0, Ranges.size()-1);
}

i64 BlockList::Search (i64 Idx, i64 Start, i64 End) {
    if (Start > End)
        return -1;

    // Note: binary search

    // check the middle of the search range
    i64   Mid     = (Start + End) / 2;
    i64   MidNxt  = Mid + 1;
    auto &RMid    = Ranges [Mid];
    auto &RMidNxt = Ranges [MidNxt];
    if (Idx >= RMid.min
        && (Idx <= RMid.max || (MidNxt < (i64)Ranges.size() && Idx < RMidNxt.min))
       )
        return Mid;

    // check lower half
    if (Idx < RMid.min)
        return Search (Idx, Start, Mid-1);

    // check upper half
    return Search (Idx, Mid+1, End);
}

// free a block index from the allocated blocks
void BlockList::Free (i64 Idx) {
    unique_lock<mutex> lock(Mtx);

    // find the range containing the block index
    i64 RangeIdx = Search (Idx);
    BlockRangeTuple &Range = Ranges[RangeIdx];
    if (RangeIdx < 0 || Range.max < Idx)
        THROW_PBEXCEPTION ("BlockList::Free: Attempt to free unallocated index: " PRIi64, Idx);

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

    // delete the range if its empty
    if (Range.min > Range.max)
        Ranges.erase(Ranges.begin()+RangeIdx);
}

// mark a block as allocated
void BlockList::MarkAllocated (i64 Idx) {
    unique_lock<mutex> lock(Mtx);

    i64 RangeIdx = Search (Idx);

    if (RangeIdx < 0) {
        // create new first range
        BlockRangeTuple R0 (Idx, Idx);
        Ranges.insert (Ranges.begin(), 1, R0);
        return;
    }

    auto &Range = Ranges [RangeIdx];
    if (Range.max >= Idx)
        THROW_PBEXCEPTION ("BlockList::MarkAllocated: Attempt to mark allocated index: " PRIi64, Idx);
    if (Range.max == Idx+1) {
        // merge new index into an existing range
        Range.max++;

        RangeIdx++;
        if (RangeIdx < (i64)Ranges.size()) {
            BlockRangeTuple &NxtRange = Ranges [RangeIdx];
            if (NxtRange.min < Range.max)
                THROW_PBEXCEPTION ("BlockList::MarkAllocated: Found unmerged range: " PRIi64, RangeIdx);
            if (NxtRange.min == Range.max) {
                Range.max = NxtRange.max;
                Ranges.erase (Ranges.begin()+RangeIdx);
            }
        }
    }
}

// convert a block number to a list of directory components
vecstr BlockList::GetSubDirs (i64 Idx) {
    vecstr SubDirs;
    i64 TmpIdx = Idx / O.BlockNumModulus;
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
string BlockList::Idx2DirString (i64 Idx) {
    vecstr Dirs = GetSubDirs (Idx);
    Dirs.insert (Dirs.begin(), 1, TopDir);
    return Utils::JoinStrs (Dirs, "/");
}

// convert a block number to a path relative to a top dir
string BlockList::Idx2FileName (i64 Idx) {
    return Idx2DirString (Idx) + "/" + to_string (Idx);
}

void BlockList::SlurpBlock (i64 Idx, string &BufStr) {
    FILE *F = Utils::OpenReadBin (Idx2FileName (Idx));

    unsigned TotalSize = 0;

    i64 BytesRead;
    do {
        BufStr.resize (TotalSize + O.ChunkSize);
        BytesRead = Utils::ReadBinary (F, BufStr.data() + TotalSize, O.ChunkSize);
        TotalSize += BytesRead;
    } while (BytesRead);

    BufStr.resize (TotalSize);

    fclose (F);
}

void BlockList::SpitBlock (i64 Idx, const string &BufStr) {
    // create subdirs
    string SubDirName = Idx2DirString(Idx);
    Utils::CreateDir (SubDirName, true);

    FILE *F = Utils::OpenWriteBin (Idx2FileName (Idx));
    Utils::WriteBinary (F, BufStr);
    fclose (F);
}

i64 BlockList::SpitNewBlock (const string &BufStr) {
    i64 Blk = Alloc();
    SpitBlock (Blk, BufStr);
    return Blk;
}
