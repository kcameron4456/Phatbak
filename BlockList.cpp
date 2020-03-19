#include "BlockList.h"
#include "Logging.h"
#include "Opts.h"
#include "Utils.h"

#include <filesystem>
#include <string>
#include <sstream>
#include <inttypes.h>
#include <stdio.h>
namespace fs = std::filesystem;

BlockList::BlockList (const string &topdir, const Opts &o) : O(o){
    TopDir = topdir;
}

BlockList::~BlockList () {
}

// allocate a block index
// always allocate the lowest availabel index
i64 BlockList::Alloc () {
    unique_lock<mutex> lock(Mtx);


    // create at head of list if idx 0 isn't allocated
    if (Ranges.size() == 0 || Ranges [0].min > 1) {
        // create first allocated range
//DBG ("Alloc: Create [0,0]\n");
        Ranges.insert (Ranges.begin(), BlockRangeTuple (0,0));
        return 0;
    }

    BlockRangeTuple &Range = Ranges[0];
//DBG ("Alloc: Range = [%d,%d]\n", (int)Range.min, (int)Range.max);

    // allocate from beginning of first range if there's room
    if (Range.min == 1)
        return Range.min = 0;

    // allocate at the end of the first range
    i64 Idx = ++Range.max;

    // see if we need to merge with the next range
    if (Ranges.size() > 1) {
        BlockRangeTuple &RangeNxt = Ranges[1];
//DBG ("Alloc: RangeNxt = [%d,%d]\n", (int)RangeNxt.min, (int)RangeNxt.max);
        if (Idx >= RangeNxt.min)
            THROW_PBEXCEPTION ("Block Allocation list corrupted. Idx:%" PRId64 " NextMin:%" PRId64, Idx, RangeNxt.min);
        if (Idx == RangeNxt.min-1) {
            // merge ranges 0 and 1
            Range.max = RangeNxt.max;
//DBG ("Alloc: Merge Range => [%d,%d] = [%d,%d]\n", (int)RangeNxt.min, (int)RangeNxt.max, (int)Range.min, (int)Range.max);
            Ranges.erase (Ranges.begin()+1);
        }
    }
//DBG ("Alloc: After: Range = [%d,%d]\n", (int)Range.min, (int)Range.max);

    return Idx;
}

// free a block index from the allocated blocks
void BlockList::Free (i64 Idx) {
    unique_lock<mutex> lock(Mtx);

    // find the range containing the block index
    i64 RangeIdx = Search (Idx);
    BlockRangeTuple Range = Ranges[RangeIdx];
//DBG ("Free (%d): Range = [%d,%d]\n", (int) Idx, (int)Range.min, (int)Range.max);
    if (RangeIdx < 0 || Range.max < Idx)
        THROW_PBEXCEPTION ("BlockList::Free: Attempt to free unallocated index: %" PRId64, Idx);

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
//DBG ("Free (%d): RNext = [%d,%d]\n", (int) Idx, (int)RNext.min, (int)RNext.max);
        Ranges.insert (Ranges.begin()+RangeIdx+1, RNext);

        Range.max = Idx-1;
    }

    // delete the range if its empty
//DBG ("Free (%d): After: Range = [%d,%d]\n", (int) Idx, (int)Range.min, (int)Range.max);
    if (Range.min > Range.max) {
//DBG ("Free (%d): Deleting\n", (int) Idx);
        Ranges.erase(Ranges.begin()+RangeIdx);
    } else {
        Ranges [RangeIdx] = Range;
    }
}

// mark a block as allocated
void BlockList::MarkAllocated (i64 Idx) {
    unique_lock<mutex> lock(Mtx);

    i64 RangeIdx = Search (Idx);
//DBG ("MarkAllocated (%d): RangeIdx= %d\n", (int)Idx, (int)RangeIdx);

    BlockRangeTuple Range;
    if (RangeIdx < 0) {
        // create new first range
//DBG ("MarkAllocated (%d): Create Front\n", (int)Idx);
        Range = {Idx, Idx};
        RangeIdx = 0;
        Ranges.insert (Ranges.begin(), 1, Range);
    } else {
        Range = Ranges [RangeIdx];
        if (Range.max >= Idx)
            THROW_PBEXCEPTION ("BlockList::MarkAllocated: Attempt to mark allocated index: %" PRId64, Idx);
//DBG ("MarkAllocated (%d): Existing Range [%d,%d]\n", (int)Idx, (int)Range.min, (int)Range.max);

        if (Idx == Range.max+1) {
            // merge with previous range
            Range.max = Idx;
//DBG ("MarkAllocated (%d): Merge\n", (int)Idx);
        } else {
            // create a new range
            RangeIdx ++;

            Range = {Idx, Idx};
//DBG ("MarkAllocated (%d): New\n", (int)Idx);
            Ranges.insert (Ranges.begin() + RangeIdx, 1, Range);
        }
    }
//DBG ("MarkAllocated (%d): Range [%d,%d]\n", (int)Idx, (int)Range.min, (int)Range.max);

    i64 RangeIdxNxt = RangeIdx + 1;
    if ((size_t)RangeIdxNxt < Ranges.size()) {
        BlockRangeTuple NxtRange = Ranges [RangeIdxNxt];
//DBG ("MarkAllocated (%d): Next Range [%d,%d]\n", (int)Idx, (int)NxtRange.min, (int)NxtRange.max);
        assert (NxtRange.min > Range.max);

        if (NxtRange.min == Range.max + 1) {
            Range.max = NxtRange.max;
//DBG ("MarkAllocated (%d): Merge Next: Range [%d,%d]\n", (int)Idx, (int)Range.min, (int)Range.max);
            Ranges.erase (Ranges.begin()+RangeIdxNxt);
        }
    }

    Ranges[RangeIdx] = Range;
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
//DBG ("Search (%d) Start:%d End:%d Mid:%d MidNxt:%d\n", (int)Idx, (int)(Start), (int)End, (int)Mid, (int)MidNxt);
//DBG ("Search (%d) RMid [%d,%d]\n", (int)Idx, (int)RMid.min, (int)RMid.max);
    if (Idx >= RMid.min
        && (MidNxt >= (i64)Ranges.size() || Idx < RMidNxt.min)
       )
        return Mid;

    // check lower half
    if (Idx < RMid.min)
        return Search (Idx, Start, Mid-1);

    // check upper half
    return Search (Idx, Mid+1, End);
}

i64 BlockList::CountAllocated () {
    i64 Total = 0;
    for (unsigned i = 0; i < Ranges.size(); i++) {
        auto &Range    = Ranges [i];
        auto &RangeNxt = Ranges [i+1];
//DBG ("CountAllocated: Range=[%" PRId64 ",%" PRId64 "] RangeNxt=[%" PRId64 ",%" PRId64 "]\n", Range.min, Range.max, RangeNxt.min, RangeNxt.max);
        assert (Range.max >= Range.min);
        assert (i == Ranges.size()-1 || Range.max < RangeNxt.min-1);
        Total += Range.max - Range.min + 1;
    }
    return Total;
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

void BlockList::Link (i64 Idx, const string &Target) {
    string Dir = Idx2DirString (Idx);
    Utils::CreateDir (Dir, 1);
    string LinkName = Dir + "/" + to_string (Idx);
    Utils::Link (LinkName, Target);
}

void BlockList::Clone (BlockList &Ref) {
    for (auto &RefRange : Ref.Ranges)
        for (i64 Idx = RefRange.min; Idx <= RefRange.max; Idx++) {
            Link (Idx, Ref.Idx2FileName(Idx));
            MarkAllocated (Idx);
        }
}

void BlockList::ReverseAlloc () {
    ReverseAlloc (TopDir);
}

void BlockList::ReverseAlloc (const string &Dir) {
    vecstr SubDirs, SubFiles;
    Utils::SlurpDir (Dir, SubDirs, SubFiles);

    for (auto SubFile : SubFiles)
        MarkAllocated (strtoull (SubFile.c_str(), NULL, 10));

    for (auto SubDir : SubDirs)
        ReverseAlloc (Dir + "/" + SubDir);
}
