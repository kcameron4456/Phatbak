#include "BlockList.h"
#include "Logging.h"
#include "Utils.h"
#include "ThreadPool.h"

#include <filesystem>
#include <string>
#include <sstream>
#include <inttypes.h>
#include <stdio.h>
namespace fs = std::filesystem;

BlockList::BlockList (const string &topdir) {
    TopDir = topdir;
}

BlockList::~BlockList () {
}

// allocate a block index
// always allocate the lowest availabel index
i64 BlockList::Alloc () {
    unique_lock<recursive_mutex> lock(Mtx);

    // create at head of list if idx 0 isn't allocated
    if (Ranges.size() == 0 || Ranges [0].min > 1) {
        // create first allocated range
        Ranges.insert (Ranges.begin(), BlockRangeTuple (0,0));
        DBG ("BlockList::Alloc Idx=0\n");
        return 0;
    }

    BlockRangeTuple &Range = Ranges[0];

    // allocate from beginning of first range if there's room
    if (Range.min == 1) {
        Range.min = 0;
        DBG ("BlockList::Alloc Idx=0\n");
        return 0;
    }

    // allocate at the end of the first range
    i64 Idx = ++Range.max;

    // see if we need to merge with the next range
    if (Ranges.size() > 1) {
        BlockRangeTuple &RangeNxt = Ranges[1];
        if (Idx >= RangeNxt.min)
            THROW_PBEXCEPTION ("Block Allocation list corrupted. Idx:%" PRId64 " NextMin:%" PRId64, Idx, RangeNxt.min);
        if (Idx == RangeNxt.min-1) {
            // merge ranges 0 and 1
            Range.max = RangeNxt.max;
            Ranges.erase (Ranges.begin()+1);
        }
    }

    DBG ("BlockList::Alloc Idx=%ld\n", Idx);
    return Idx;
}

// free a block index from the allocated blocks
void BlockList::Free (i64 Idx) {
    unique_lock<recursive_mutex> lock(Mtx);
    assert (Idx >= 0);

    // find the range containing the block index
    i64 RangeIdx = Search (Idx);
    BlockRangeTuple Range = Ranges[RangeIdx];
    if (RangeIdx >= 0)
        Range = Ranges[RangeIdx];
    if (RangeIdx < 0 || Range.max < Idx)
        THROW_PBEXCEPTION ("BlockList::Free (%s) Attempt to free unallocated index: %" PRId64, TopDir.c_str(), Idx);

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
    if (Range.min > Range.max) {
        Ranges.erase(Ranges.begin()+RangeIdx);
    } else {
        Ranges [RangeIdx] = Range;
    }
}

bool BlockList::IsAllocated (i64 Idx) {
    unique_lock<recursive_mutex> lock(Mtx);
    assert (Idx >= 0);

    // find the range containing the block index
    i64 RangeIdx = Search (Idx);
    if (RangeIdx < 0)
        return false;

    BlockRangeTuple Range = Ranges[RangeIdx];
    assert (Range.min <= Range.max);
    assert (Range.min <= Idx);
    return Range.max >= Idx;
}

// mark a block as allocated
void BlockList::MarkAllocated (i64 Idx) {
    unique_lock<recursive_mutex> lock(Mtx);
    assert (Idx >= 0);

    i64 RangeIdx = Search (Idx);

    BlockRangeTuple Range;
    if (RangeIdx < 0) {
        // create new first range
        Range = {Idx, Idx};
        RangeIdx = 0;
        Ranges.insert (Ranges.begin(), 1, Range);
    } else {
        Range = Ranges [RangeIdx];
        if (Range.max >= Idx)
            THROW_PBEXCEPTION ("BlockList::MarkAllocated: Attempt to mark allocated index: %" PRId64, Idx);

        if (Idx == Range.max+1) {
            // merge with previous range
            Range.max = Idx;
        } else {
            // create a new range
            RangeIdx ++;

            Range = {Idx, Idx};
            Ranges.insert (Ranges.begin() + RangeIdx, 1, Range);
        }
    }

    i64 RangeIdxNxt = RangeIdx + 1;
    if ((size_t)RangeIdxNxt < Ranges.size()) {
        BlockRangeTuple NxtRange = Ranges [RangeIdxNxt];
        assert (NxtRange.min > Range.max);

        if (NxtRange.min == Range.max + 1) {
            Range.max = NxtRange.max;
            Ranges.erase (Ranges.begin()+RangeIdxNxt);
        }
    }

    Ranges[RangeIdx] = Range;
}

// find the last tuple whose max value is less than or equal to a given index
i64 BlockList::Search (i64 Idx) const {
    assert (Idx >= 0);
    return Search (Idx, 0, Ranges.size()-1);
}

i64 BlockList::Search (i64 Idx, i64 Start, i64 End) const {
    if (Start > End)
        return -1;

    // Note: binary search

    // check the middle of the search range
    i64   Mid     = (Start + End) / 2;
    i64   MidNxt  = Mid + 1;
    auto &RMid    = Ranges [Mid];
    auto &RMidNxt = Ranges [MidNxt];
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

i64 BlockList::CountAllocated () const {
    i64 Total = 0;
    for (unsigned i = 0; i < Ranges.size(); i++) {
        auto &Range    = Ranges [i];
        auto &RangeNxt = Ranges [i+1];
        assert (Range.max >= Range.min);
        assert (i == Ranges.size()-1 || Range.max < RangeNxt.min-1);
        Total += Range.max - Range.min + 1;
    }
    return Total;
}

// convert a block number to a list of directory components
vecstr BlockList::GetSubDirs (i64 Idx) const {
    assert (Idx >= 0);
    vecstr SubDirs;
    i64 TmpIdx = Idx / O.BlockNumModulus;
    while (TmpIdx) {
        unsigned Part   = TmpIdx % O.BlockNumModulus;
                 TmpIdx = TmpIdx / O.BlockNumModulus;

        SubDirs.push_back (string ("d") + to_string (Part));
    }
DBG ("BlockList::GetSubDirs TopDir=%s Idx=%ld SubDirs=%s\n", TopDir.c_str(), Idx, Utils::JoinStrs(SubDirs, " ").c_str());
    return SubDirs;
}

// convert an index into a string containing only subdirs
string BlockList::Idx2SubDirString (i64 Idx) const {
    vecstr SubDirs = GetSubDirs (Idx);
    return Utils::JoinStrs (SubDirs, "/");
}

// convert a block number to a directory path
string BlockList::Idx2DirString (i64 Idx) const {
    return Idx2DirString (Idx, TopDir);
}

string BlockList::Idx2DirString (i64 Idx, const string &Top) const {
    return Top + "/" + Idx2SubDirString (Idx);
}

// convert a block number to a path relative to the top dir
string BlockList::Idx2FileName (i64 Idx) const {
    string Name = Idx2DirString (Idx) + "/" + to_string (Idx);
DBG ("BlockList::Idx2FileName TopDir=%s Idx=%ld Name=%s\n", TopDir.c_str(), Idx, Name.c_str());
    return Name;
}

void BlockList::SlurpBlock (i64 Idx, string &BufStr) const {
    assert (Idx >= 0);
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
    assert (Idx >= 0);
    // create subdirs
    string SubDirName = Idx2DirString(Idx);
    Utils::CreateDir (SubDirName, true);

    FILE *F = Utils::OpenWriteBin (SubDirName + "/" + to_string (Idx));
    Utils::WriteBinary (F, BufStr);
    fclose (F);
}

i64 BlockList::SpitNewBlock (const string &BufStr) {
    i64 Blk = Alloc();
    SpitBlock (Blk, BufStr);
    return Blk;
}

void BlockList::Link (i64 Idx, const string &TargTop) {
    assert (Idx >= 0);
    string DirStr  = Idx2SubDirString (Idx);
    string IdxStr  = to_string        (Idx);

    string Dir     = TopDir + "/" + DirStr;
    Utils::CreateDir (Dir, 1);

    string LinkName = Dir                    + "/" + IdxStr;
    string Target   = TargTop + "/" + DirStr + "/" + IdxStr;
    Utils::Link (LinkName, Target);
}

void BlockList::ReverseAlloc () {
    ReverseAlloc (TopDir);
}

void BlockList::ReverseAlloc (const string &Dir) {
    vecstr SubDirs, SubFiles;
    Utils::SlurpDir (Dir, SubDirs, SubFiles);

    for (auto SubDir : SubDirs) {
        function <void()> Task = [=,this](){ReverseAlloc (Dir + "/" + SubDir);};
        ThreadPool.Execute (Task, 0);
    }

    for (auto SubFile : SubFiles) {
        function <void()> Task = [=,this](){MarkAllocated (strtoull (SubFile.c_str(), NULL, 10));};
        ThreadPool.Execute (Task, 0);
    }
}
