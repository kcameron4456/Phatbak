#include "ArchiveCreate.h"
#include "Logging.h"
#include "Opts.h"
#include "Hash.h"

#include <filesystem>
#include <string>
#include <sstream>
#include <stdio.h>
namespace fs = std::filesystem;

ArchiveCreate::ArchiveCreate (RepoInfo *repo, const string &name) {
    Init (repo, name);
}

ArchiveCreate::~ArchiveCreate () {
    time_t EndTime = time(NULL);
    fprintf (LogFile, "Backup Ended At: %s", ctime(&EndTime));

    int Secs = difftime (EndTime, O.StartTime);
    fprintf (LogFile, "Elasped Time: %d seconds\n", Secs);

    fclose (LogFile);
    fclose (ListFile);
}

void ArchiveCreate::Init (RepoInfo *repo, const string &name) {
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

void ArchiveCreate::PushFileList (const string &Fname, BlockIdxType Block, char Comp, const string &Hash) {
    string FileEntry = Fname + " /../ " + to_string(Block) + " " + Comp + " " + Hash + "\n";
    fputs (FileEntry.c_str(), ListFile);
}

ArchFileCreate::ArchFileCreate (ArchiveCreate *arch) {
    ArchCreate = arch;

    Mtx.lock();
}

void ArchFileCreate::Create (LiveFile &LF) {
    Name  = LF.Name;
    Stats = LF.MakeInfoHeader ();

    // Create Finfo file contents
    string FInfo = Stats + "\n";

    // For files, create chunks and FInfo entries
    if (LF.IsFile()) {
        char Chunk [O.ChunkSize];
        LF.OpenRead();
        while (int RdSize = LF.ReadChunk (Chunk)) {
            // write the chunk to the archive
            BlockIdxType ChnkIdx = ArchCreate->ChunkBlocks.Alloc();
            FILE *ChunkF = ArchCreate->ChunkBlocks.OpenBlockFile (ChnkIdx, "wb");
            if (fwrite (Chunk, RdSize, 1, ChunkF) != 1)
                THROW_PBEXCEPTION_IO ("Error writing to Chunk block file: " + ArchCreate->ChunkBlocks.Idx2FileName(ChnkIdx));
            fclose (ChunkF);

            // compute hash
            Hash Hasher (O.HashType);
            Hasher.Update (Chunk, RdSize);
            string HashHex = Hasher.GetHash();

            // add chunk to finfo
            FInfo += "U: " + to_string (ChnkIdx) + " " + HashHex + "\n";
        }
        LF.Close();
    }
    if (LF.IsSLink()) {
        FInfo += "L: " + LF.Target + "\n";
    }

    // Create the file info block in the archive
    InfoBlkNum = ArchCreate->FInfoBlocks.Alloc();
    FILE *FInfoF = ArchCreate->FInfoBlocks.OpenBlockFile (InfoBlkNum, "wb");
    if (fwrite (FInfo.c_str(), FInfo.size(), 1, FInfoF) != 1)
        THROW_PBEXCEPTION_IO ("Error writing to FInfo block file: " + ArchCreate->FInfoBlocks.Idx2FileName(InfoBlkNum));
    fclose (FInfoF);

    // get the finfo block hash
    Hash Hasher (O.HashType);
    Hasher.Update (FInfo.c_str(), FInfo.size());
    InfoBlkHash = Hasher.GetHash();

    // TBD: compress
    InfoBlkComp = 'U';

    // update archive file list
    ArchCreate->PushFileList (Name, InfoBlkNum, InfoBlkComp, InfoBlkHash);

    // flag completion
    Mtx.unlock();
}

// link to previously archived file
void ArchFileCreate::CreateLink (LiveFile &LF, ArchFileCreate *Prev) {
    Name  = LF.Name;

    // wait for processing of original file to complete
    Prev->Mtx.lock();

    ArchCreate->PushFileList (Name, Prev->InfoBlkNum, Prev->InfoBlkComp, Prev->InfoBlkHash);

    // release prev file
    Prev->Mtx.unlock();

    // release this file
    Mtx.unlock();
}
