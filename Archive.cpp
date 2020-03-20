#include "Archive.h"
#include "Logging.h"
#include "Utils.h"
#include "ThreadPool.h"
#include "Comp.h"
using namespace Utils;

#include <string>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////
Archive::Archive(RepoInfo *repo, const string &name) {
    DBGCTOR;
    Repo         = repo;
    Name         = name;
    ArchDirPath  = Repo->Name + "/" + Name;
    IDPath       = ArchDirPath + "/" + PHATBAK_ARCH_ID;
    ListPath     = ArchDirPath + "/List";
    LogPath      = ArchDirPath + "/PhatBak.log";
    OptionsPath  = ArchDirPath + "/Options";
    FinfoDirPath = ArchDirPath + "/FInfo";
    ChunkDirPath = ArchDirPath + "/Chunks";
    ExtraDirPath = ArchDirPath + "/Extra";
}

Archive::~Archive() {
    DBGDTOR;
    LogFile .close();
    ListFile.close();
}

FileListEntry Archive::ParseListLine (const string &ListLine, u64 LineNo) {
    FileListEntry Res;

    // parse file list entry
    // separate filename from attributes
    vecstr FirstCut = SplitStr (ListLine, " /../ ");
    if (FirstCut.size() != 2)
        THROW_PBEXCEPTION_FMT ("%s:%llu has bad format", ListPath.c_str(), LineNo);
    Res.Name = FirstCut[0];

    // separate fields of rhs
    vecstr RHSToks = SplitStr (FirstCut[1], " ");
    if (RHSToks.size() != 3)
        THROW_PBEXCEPTION_FMT ("%s:%llu has bad format", ListPath.c_str(), LineNo);
    Res.BlkNum   = stoull (RHSToks[0]);
    Res.CompType = Comp::CompFlag2CompType(RHSToks[1][0], O);
    Res.Hash     = RHSToks[2];

    return Res;
}

//////////////////////////////////////////////////////////////////////
ArchiveRead::ArchiveRead (RepoInfo *repo, const string &name) : Archive (repo, name) {
    DBGCTOR;
    ParseOptions ();

    if (!fs::exists (IDPath))
        THROW_PBEXCEPTION_FMT ("%s don't exist");

    FInfoBlocks = new BlockList (FinfoDirPath, O);
    ChunkBlocks = new BlockList (ChunkDirPath, O);
}

ArchiveRead::~ArchiveRead() {
    DBGDTOR;
}

void ArchiveRead::ParseOptions () {
    // extract options from the archive file
    fstream OptsFile = OpenReadStream (OptionsPath);
    string OptLine;
    while (getline (OptsFile, OptLine)) {
        // split into name/value pairs
        vecstr Toks = SplitStr (OptLine, "=");

        // skip non-option lines
        if (Toks.size() != 2)
            continue;

        // clean up tokens
        for (auto &Tok : Toks)
            TrimStr (&Tok);

        // rename tokens
        string &OptName = Toks[0];
        string &OptVal  = Toks[1];

             if (OptName == "BlockNumDigits" ) O.BlockNumDigits  = stoull               (OptVal);
        else if (OptName == "BlockNumModulus") O.BlockNumModulus = stoull               (OptVal);
        else if (OptName == "ChunkSize"      ) O.ChunkSize       = stoull               (OptVal);
        else if (OptName == "HashType"       ) O.HashType        = HashNameToEnum       (OptVal);
        else if (OptName == "CompType"       ) O.CompType        = Comp::CompNameToEnum (OptVal);
        else if (OptName == "CompLevel"      ) O.CompLevel       = stoull               (OptVal);
    }

    OptsFile.close();
}

void ArchiveRead::DoExtractJob (const FileListEntry &ListEntry) {
    // extract information about the archived file
    ArchFileRead *AF = new ArchFileRead (this, ListEntry);

    InfoBlockIdsMtx.lock();
    bool DoHLink = InfoBlockIds.find (AF->ListEntry.BlkNum) != InfoBlockIds.end();
    string LTarget = DoHLink ? InfoBlockIds[AF->ListEntry.BlkNum]
                             : AF->LinkTarget;
    InfoBlockIdsMtx.unlock();

    // create extracted file
    LiveFile *LF = new LiveFile (AF->Name, AF->Stats, LTarget,
                                 AF->Chunks, ChunkBlocks,
                                 ModTimes, &ModTimesMtx,
                                 DoHLink);

    if (!DoHLink) {
        InfoBlockIdsMtx.lock();
        InfoBlockIds[AF->ListEntry.BlkNum] = LF->Name;
        InfoBlockIdsMtx.unlock();
    }

    delete LF;
    delete AF;
}

void ArchiveRead::DoExtract () {
    // open the file list
    auto ListFile = OpenReadStream (ListPath);

    // parse and extract all the entries in the list
    u64 LineNo = 0;
    string FileLine;
    while (getline (ListFile, FileLine)) {
        LineNo ++;
        FileListEntry ListEntry = ParseListLine (FileLine, LineNo);

        if (O.NumThreads) {
            JobCtrl *Job = ThreadPool.AllocThread();
            Job->JobType = JobCtrl::ExtractFile;
            Job->ExtractFileInfo.Arch      = this;
            Job->ExtractFileInfo.ListEntry = ListEntry;
            Job->Go();
        } else {
            DoExtractJob (ListEntry);
        }
    }

    // wait for all jobs to finish
    ThreadPool.WaitIdle();

    // handle defered modification times
    for (auto& [Name, Time]: ModTimes)
        SetModTime (Name, NsToTimeSpec(Time));

    ListFile.close();
}

//////////////////////////////////////////////////////////////////////
ArchiveReference::ArchiveReference (RepoInfo *repo, const string &name) : ArchiveRead (repo, name) {
    // create a list of files with first-order info
    fstream FL = OpenReadStream (ListPath);
    string Line;
    u64 LineCount = 1;
//DBG ("ArchiveReference this=%p\n", this);
    while (getline (FL, Line)) {
        FileListEntry FLE = ParseListLine (Line, LineCount);
//DBG ("ArchiveReference Name=%s\n", FLE.Name.c_str());
        FileMap [FLE.Name] = FLE;
        LineCount ++;
    }
    FL.close();

    // load the finfo and chunk blocklists based on existing files
    FInfoBlocks->ReverseAlloc ();
    ChunkBlocks->ReverseAlloc ();
}

ArchiveReference::~ArchiveReference () {
}

//////////////////////////////////////////////////////////////////////
ArchiveCreate::ArchiveCreate (RepoInfo *repo, const string &name, ArchiveReference *ref) : Archive (repo, name) {
    DBGCTOR;
    ArchRef = ref;

    // create archive dir
    if (fs::exists (ArchDirPath))
        THROW_PBEXCEPTION ("Archive (%s) already exists. Can't overwrite", ArchDirPath.c_str());
    CreateDir (ArchDirPath);

    // mark it as a PhatBak archive
    Touch (IDPath);

    // create the log file
    LogFile = OpenWriteStream (LogPath);
    LogFile << "Backup Started At: " << asctime(localtime(&O.StartTime)) << endl;

    // create Options file
    fstream OptFile = OpenWriteStream (OptionsPath);
    O.Print (OptFile);
    OptFile.close();

    // create new archive subdirs
    for (auto SubDir : {ChunkDirPath, FinfoDirPath, ExtraDirPath})
        CreateDir (SubDir);

    // initialize block allocators
    FInfoBlocks = new BlockList (FinfoDirPath, O);
    ChunkBlocks = new BlockList (ChunkDirPath, O);

    // if using a reference arch, clone the block lists
    if (ArchRef) {
        FInfoBlocks->Clone (*ArchRef->FInfoBlocks);
        ChunkBlocks->Clone (*ArchRef->ChunkBlocks);
    }

    // start the file list
    ListFile = OpenWriteStream (ListPath);
}

ArchiveCreate::~ArchiveCreate () {
    DBGDTOR;

    ThreadPool.WaitIdle ();

    time_t EndTime = time(NULL);
    LogFile << "Backup Ended At: " << ctime(&EndTime) << endl;

    int Secs = difftime (EndTime, O.StartTime);
    LogFile << "Elasped Time: " << Secs << " seconds\n";

    LogFile .close();
    ListFile.close();

    delete ChunkBlocks;
    delete FInfoBlocks;
}

void ArchiveCreate::PushFileList (const FileListEntry &ListEntry) {
    string FileLine = ListEntry.Name                               + " /../ "
                     + to_string(ListEntry.BlkNum)                 + " "
                     + Comp::CompType2CompFlag(ListEntry.CompType) + " "
                     + ListEntry.Hash                              + "\n"
                     ;

    // prevent corruption when multiple threads are creating file entries
    static mutex LocalMtx;
    LocalMtx.lock();

    ListFile << FileLine;

    LocalMtx.unlock();
}

//////////////////////////////////////////////////////////////////////
ArchFile::ArchFile (Archive *arch) {
    DBGCTOR;
    Arch = arch;
}

ArchFile::~ArchFile () {
    DBGDTOR;
}

//////////////////////////////////////////////////////////////////////
ArchFileRead::ArchFileRead (ArchiveRead *arch, const FileListEntry &listentry) : ArchFile (arch) {
    DBGCTOR;
    Arch      = arch;
    ListEntry = listentry;
    Name      = ListEntry.Name;

    // extract information from the FInfo block
    string FInfoPacked;
    Arch->FInfoBlocks->SlurpBlock (ListEntry.BlkNum, FInfoPacked);

    // decompress
    string *SelData = &FInfoPacked;
    string  DeCompressed;
    if (ListEntry.CompType != CompType_NONE) {
        Comp::DeCompress (ListEntry.CompType, FInfoPacked, DeCompressed);
        SelData = &DeCompressed;
    }

    // check hash
    string FInfoHashFound = HashStr (O.HashType, *SelData);
    if (FInfoHashFound != ListEntry.Hash)
        THROW_PBEXCEPTION_FMT ("Hash mismatch on FInfo block #%llu", ListEntry.BlkNum);

    // parse finfo
    vecstr FInfoLines = SplitStr (*SelData, "\n");
    for (auto Line : FInfoLines) {
        if (Line.size() < 3 || Line[1] != '-')
            THROW_PBEXCEPTION_FMT ("Illegal FInfo format: %s", Line.c_str());
        char RecType = Line[0];
        Line.erase (0,2);
        switch (RecType) {
            case 'H' :
                Stats = ParseStatsHeader (Line); break;

            case 'L' :
                LinkTarget = Line; break;

            case 'U' :
            case 'C' : {
                vecstr Parts = SplitStr (Line, " ");
                Chunks.emplace_back (RecType == 'U' ? CompType_NONE : O.CompType,
                                     stoull (Parts[0].c_str()),
                                     Parts[1],
                                     O.HashType);
                break;
                }

            default :
                THROW_PBEXCEPTION_FMT ("Unrecognized FInfo record type '%c'", RecType);
        };
    }
}

ArchFileRead::~ArchFileRead () {
    DBGDTOR;
}

//////////////////////////////////////////////////////////////////////
ArchFileCreate::ArchFileCreate (ArchiveCreate *arch, LiveFile *lf) : ArchFile (arch) {
    DBGCTOR;
    LF     = lf;
    Name   = LF->Name;

    Mtx.lock();
}

void ArchFileCreate::HashAndCompressJob (const string &ChunkData
                                        ,ChunkInfo *RefChunkInfo, BlockList *RefBlockList
                                        ,HashAndCompressReturn *HACR) {
    // compute hash
    Hash Hasher (O.HashType);
    HACR->HashHex = Hasher.HashStr(ChunkData);

    // compare to reference hash
    if (RefChunkInfo && HACR->HashHex == RefChunkInfo->Hash) {
        // keep cloned chunk
        HACR->CompFlag = RefChunkInfo->CompType == CompType_NONE ? CompFlagUnComp : CompFlagComp;
        HACR->BlockIdx = RefChunkInfo->Idx;
    } else {
        // create fresh chunk

        // unlink cloned chunk block
        if (RefChunkInfo)
            RefBlockList->UnLink (RefChunkInfo->Idx);

        // compress the chunk
        // if compression doesn't help, keep it uncompressed
        const string *SelChunk = &ChunkData;
        HACR->CompFlag = CompFlagUnComp;
        string Compressed;
        if (O.CompType != CompType_NONE) {
            Comp::Compress (ChunkData, Compressed);
            if (Compressed.size() < ChunkData.size()) {
                SelChunk       = &Compressed;
                HACR->CompFlag =  CompFlagComp;
            }
        }

        // write the chunk to archive
        HACR->BlockIdx = Arch->ChunkBlocks->SpitNewBlock (*SelChunk);
    }

    // notify the caller that hash and compress are complete
    HACR->BL.PostIdle();
}

void ArchFileCreate::CreateJob (bool Keep) {
    // get matching file info from reference archive
DBG ("CreateJob Name=%s\n", Name.c_str());
    ArchiveReference *RefArch = ((ArchiveCreate*)Arch)->ArchRef;
    ArchFileRead          *RF = NULL;
    FileListEntry          FileEntry;
    if (RefArch && RefArch->FileMap.count (Name)) {
        FileEntry = RefArch->FileMap[Name];

        // grab info from the reference archive
        RF = new ArchFileRead ((ArchiveRead *)Arch, FileEntry);
DBG ("CreateJob Name Match\n");
DBG ("LF size = %ld  RF size = %ld\n", LF->Stats.st_size, RF->Stats.st_size);
    }

    // if the reference is too different,
    // eliminate linked copy in new archive
    if (RF && (
         (                 (LF->Stats.st_mode != RF->Stats.st_mode))
      || (LF->IsFile()  && (LF->Stats.st_size != RF->Stats.st_size))
      || (LF->IsSLink() && (LF->LinkTarget    != RF->LinkTarget   ))
       )) {

//DBG ("Unlinking\n");
        // eliminate hard-linked chunk blocks
        for (auto &Chunk : RF->Chunks) {
//DBG ("Unlinking Chunk %ld\n", Chunk.Idx);
            Arch->ChunkBlocks->UnLink (Chunk.Idx);
        }

        // eliminate the FInfo block
        Arch->FInfoBlocks->UnLink (FileEntry.BlkNum);

        // elimintate the File from the reference filemap
        RefArch->FileMapMtx.lock();
        RefArch->FileMap   .erase (Name);
        RefArch->FileMapMtx.unlock();

        delete RF;
        RF = NULL;
    }

    // Create Finfo file contents
    string FInfo = "H-" + CreateStatsHeader (LF->Stats) + "\n";

    // For files, create chunks
    if (LF->IsFile()) {
        vector <HashAndCompressReturn *> Returns;

        string ChunkData;
        u32    ChunkIdx = 0;    
        LF->OpenRead();
        while (LF->ReadChunk (ChunkData)) {
            HashAndCompressReturn *Return = new HashAndCompressReturn;
            Returns.push_back (Return);

            ChunkInfo *RefChunkInfo = NULL;
            BlockList *RefBlockList = NULL;
            if (RF && ChunkIdx < RF->Chunks.size()) {
                RefChunkInfo = &RF->Chunks[ChunkIdx];
                RefBlockList =  RF->Arch->ChunkBlocks;
            }

            // try to allocate a thread
            JobCtrl *Thr = ThreadPool.AllocThread(0);
            if (Thr) {
                // get help
                Thr->JobType                        = JobCtrl::CompressChunk;
                Thr->CompressChunkInfo.AF           = this;
                Thr->CompressChunkInfo.RefChunkInfo = RefChunkInfo;
                Thr->CompressChunkInfo.RefBlockList = RefBlockList;
                Thr->CompressChunkInfo.HACR         = Return;
                Thr->Go();
            } else {
                // do it ourselves
                HashAndCompressJob (ChunkData, RefChunkInfo, RefBlockList, Return);
            }

            ChunkIdx++;
        }
        LF->Close();

        for (auto Return : Returns) {
            // wait for the job to complete
            Return->BL.WaitIdle();

            // add chunk to finfo
            FInfo += string("") + Return->CompFlag + "-" + to_string (Return->BlockIdx) + " " + Return->HashHex + "\n";

            delete Return;
        }
    } else if (LF->IsSLink()) {
        FInfo += "L-" + LF->LinkTarget + "\n";
    }

    // compress the finfo
    // if compression doesn't help, keep it uncompressed
    string *SelFInfo = &FInfo;
    ListEntry.CompType = CompType_NONE;
    string Compressed;
    if (O.CompType != CompType_NONE) {
        Comp::Compress (FInfo, Compressed);
        if (Compressed.size() < FInfo.size()) {
            SelFInfo    = &Compressed;
            ListEntry.CompType = O.CompType;
        }
    }

    // Put the file info block in the archive
    ListEntry.BlkNum = Arch->FInfoBlocks->SpitNewBlock (*SelFInfo);

    // get the finfo block hash
    Hash Hasher (O.HashType);
    ListEntry.Hash = Hasher.HashStr(FInfo);

    // update archive file list
    ListEntry.Name = Name;
    ((ArchiveCreate*)Arch)->PushFileList (ListEntry);

    // flag completion
    Mtx.unlock();

    if (RF)
        delete RF;

    // we're done with the Live File
    delete LF;

    // delete this if we know it won't be needed again
    if (!Keep)
        delete this;
}

void ArchFileCreate::Create (bool Keep) {
    if (O.NumThreads) {
        JobCtrl *Job = ThreadPool.AllocThread();
        Job->JobType = JobCtrl::CreateFile;
        Job->CreateFileInfo.AF   = this;
        Job->CreateFileInfo.Keep = Keep;
        Job->Go();
    } else {
        CreateJob (Keep);
    }
}

// link to previously archived file
void ArchFileCreate::CreateLink (ArchFileCreate *Prev) {
    // wait for processing of original file to complete
    Prev->Mtx.lock();

    ListEntry      = Prev->ListEntry;
    ListEntry.Name = Name;
    ((ArchiveCreate*)Arch)->PushFileList (ListEntry);

    // release prev file
    Prev->Mtx.unlock();

    // release this file (even though nobody will be waiting for this one)
    Mtx.unlock();

    // we're done with the Live File
    delete LF;

    // this archive file won't be used again
    delete this;
}

//////////////////////////////////////////////////////////////////////
