#include "Archive.h"
#include "Logging.h"
#include "Opts.h"
#include "Hash.h"

#include <filesystem>
#include <string>
#include <sstream>
#include <stdio.h>
namespace fs = std::filesystem;

Archive::Archive (RepoInfo *repo, const string &name) {
    Init (repo, name);
}

void Archive::Init (RepoInfo *repo, const string &name) {
    Repo = repo;
    Name = name;
    ArchDirName = Repo->Name + "/" + Name;

    // create archive dir
    if (fs::exists (ArchDirName))
        THROW_PBEXCEPTION_FMT ("Archive dir (%s) already exists, can't overwrite", ArchDirName.c_str());
    error_code ec;
    if (!fs::create_directory(ArchDirName, ec))
        THROW_PBEXCEPTION_IO ("Can't create Archive directory: " + ArchDirName);

    // create the log file
    string LogFileN = ArchDirName + "/PhatBak.log";
    if ((LogFile = fopen (LogFileN.c_str(), "w")) < 0)
        THROW_PBEXCEPTION_IO ("Can't create Log file: " + LogFileN);
    fprintf (LogFile, "Backup Started At: %s", asctime(localtime(&O.StartTime)));

    // create Options file
    string OptFileN = ArchDirName + "/Options";
    FILE *OptFile;
    if ((OptFile = fopen (OptFileN.c_str(), "w")) < 0)
        THROW_PBEXCEPTION_IO ("Can't create Options file: " + OptFileN);
    O.Print (OptFile);
    fclose (OptFile);

    // create the file list
    string ListN = ArchDirName + "/List";
    if ((ListFile = fopen (ListN.c_str(), "w")) < 0)
        THROW_PBEXCEPTION_IO ("Can't create List file: " + ListN);

    // create new archive subdirs
    for (auto SubDir : {"Chunks", "FInfo", "Extra"}) {
        error_code ec;
        string SubDirN = ArchDirName + "/" + SubDir;
        if (!fs::create_directory (SubDirN, ec))
            THROW_PBEXCEPTION_IO ("Can't create SubDir: " + SubDirN);
    }

    // initialize block allocators
    FInfoBlocks.Init (ArchDirName + "/FInfo");
    ChunkBlocks.Init (ArchDirName + "/Chunks");
}

void Archive::PushFileList (const string &Fname, BlockIdxType Block, const string &Hash) {
    string FileEntry = Fname + " /../ " + to_string(Block) + " U " + Hash + "\n";
    fputs (FileEntry.c_str(), ListFile);
}

ArchFile::ArchFile (Archive *arch) {
    Arch = arch;
}

void ArchFile::Create (LiveFile &lf) {
    Name  = lf.Name;
    Stats = lf.MakeInfoHeader ();

    // Create Finfo file contents
    string FInfo = Stats + "\n";

    // For files, create chunks and FInfo entries
    if (lf.IsFile()) {
        char Chunk [O.ChunkSize];
        lf.OpenRead();
        while (int RdSize = lf.ReadChunk (Chunk)) {
            // write the chunk to the archive
            BlockIdxType ChnkIdx = Arch->ChunkBlocks.Alloc();
            FILE *ChunkF = Arch->ChunkBlocks.OpenBlockFile (ChnkIdx, "wb");
            if (fwrite (Chunk, RdSize, 1, ChunkF) != 1)
                THROW_PBEXCEPTION_IO ("Error writing to Chunk block file: " + Arch->ChunkBlocks.Idx2FileName(ChnkIdx));
            fclose (ChunkF);

            // compute hash
            Hash Hasher (O.HashType);
            Hasher.Update (Chunk, RdSize);
            string HashHex = Hasher.GetHash();

            // add chunk to finfo
            FInfo += "U: " + to_string (ChnkIdx) + " " + HashHex + "\n";
        }
        lf.Close();
    }
    if (lf.IsSLink()) {
        FInfo += "L: " + lf.Target + "\n";
    }

    // Create the file info block in the archive
    BlockIdxType BlkIdx = Arch->FInfoBlocks.Alloc();
    FILE *FInfoF = Arch->FInfoBlocks.OpenBlockFile (BlkIdx, "wb");
    if (fwrite (FInfo.c_str(), FInfo.size(), 1, FInfoF) != 1)
        THROW_PBEXCEPTION_IO ("Error writing to FInfo block file: " + Arch->FInfoBlocks.Idx2FileName(BlkIdx));
    fclose (FInfoF);

    // get the finfo block hash
    Hash Hasher (O.HashType);
    Hasher.Update (FInfo.c_str(), FInfo.size());
    string HashHex = Hasher.GetHash();

    // update archive file list
    Arch->PushFileList (Name, BlkIdx, HashHex);
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
    BlockIdxType TmpBlk = Idx / O.BlockNumModulus;
    while (TmpBlk) {
        BlockIdxType Part   = TmpBlk % O.BlockNumModulus;
                     TmpBlk = TmpBlk / O.BlockNumModulus;

        stringstream SubDir;
        SubDir << "d" << setfill('0') << setw (O.BlockNumDigits) << Part;
        SubDirs.push_back (SubDir.str());
    }
    return SubDirs;
}

// convert a block number to a directory path
string BlockList::Idx2DirString (BlockIdxType Idx) {
    string DirString = TopDir;
    vector <string> SubDirs = GetSubDirs (Idx);
    for (auto SubDir : SubDirs)
        DirString += "/" + SubDir;
    return DirString;
}

// convert a block number to a path relative to a top dir
string BlockList::Idx2FileName (BlockIdxType Idx) {
    return Idx2DirString (Idx) + "/" + to_string (Idx);
}

FILE *BlockList::OpenBlockFile (BlockIdxType Idx, const char *mode) {
    string BlockFileName = TopDir;

    // create subdirs
    vector <string> SubDirs = GetSubDirs (Idx);
    for (auto SubDir : SubDirs) {
        BlockFileName += "/" + SubDir;
        if (!fs::exists (BlockFileName) && !fs::create_directory (BlockFileName))
            THROW_PBEXCEPTION_IO ("Can't create directory: " + BlockFileName);
    }

    BlockFileName += "/" + to_string (Idx);
    FILE *BlkFile = fopen (BlockFileName.c_str(), mode);
    if (!BlkFile)
        THROW_PBEXCEPTION_IO ("Can't open block file: " + BlockFileName);

    return BlkFile;
}
