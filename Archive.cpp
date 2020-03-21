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
    vecstr FirstCut = SplitStr (ListLine, ListRecSep);
    if (FirstCut.size() != 2 && FirstCut.size() != 3)
        THROW_PBEXCEPTION_FMT ("%s:%llu has bad format", ListPath.c_str(), LineNo);

    // results
    Res.Name          = FirstCut[0];
    Res.CompFlag      = CompFlagUnComp;
    Res.Stats.st_mode = 0;
    Res.LinkTarget    = "";
    Res.FInfoIdx      = -1;
    Res.LineNo        = LineNo;

    // separate fields of rhs
    vecstr RHSToks = SplitStr (FirstCut[1], " ");
    for (auto &RHSTok : RHSToks) {
        vecstr Toks = SplitStr (RHSTok, ":");
        string &Name = Toks[0];
        string &Val  = Toks[1];
             if (Name == "mode" ) Res.Stats.st_mode =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "uid"  ) Res.Stats.st_uid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "gid"  ) Res.Stats.st_gid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "size" ) Res.Stats.st_size =               strtoull (Val.c_str(), NULL, 10);
        else if (Name == "mtime") Res.Stats.st_mtim = NsToTimeSpec (strtoull (Val.c_str(), NULL, 16));
        else if (Name == "U"    ) {
                                  Res.FInfoIdx      =               strtoull (Val.c_str(), NULL, 10);
                                  Res.CompFlag      =               CompFlagUnComp;
                                  }
        else if (Name == "C"    ) {
                                  Res.FInfoIdx      =               strtoull (Val.c_str(), NULL, 10);
                                  Res.CompFlag      =               CompFlagComp;
                                  }
        else
            THROW_PBEXCEPTION_FMT ("Illegal entry in %s:%llu : %s", ListPath.c_str(), LineNo, RHSTok.c_str());
    }

    // parse optional third field
    // only slink
    if (FirstCut.size() == 3) {
        if (FirstCut[2].substr(0,6) != "slink:")
            THROW_PBEXCEPTION_FMT ("Illegal entry in %s:%llu : %s", ListPath.c_str(), LineNo, FirstCut[2].c_str());
        Res.LinkTarget = FirstCut[2].substr(6);
    }

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

void ArchiveRead::DoExtractJob (const string &ListLine, u64 LineNo) {
    FileListEntry ListEntry = ParseListLine (ListLine, LineNo);

    // extract information about the archived file
    ArchFileRead *AF = new ArchFileRead (this, ListEntry);

    // keep track of finfo block ids
    // if one's been seen before, create a hard link
    bool          DoHLink = false;
    HLinkSyncRec *HLS     = NULL;
    if (AF->ListEntry.FInfoIdx >= 0) {
        HLinkSyncsMtx.lock();

        DoHLink = HLinkSyncs.count (AF->ListEntry.FInfoIdx) != 0;
        if (DoHLink) {
            HLS = HLinkSyncs[AF->ListEntry.FInfoIdx];
        } else  {
            HLS = new HLinkSyncRec;
            HLinkSyncs [AF->ListEntry.FInfoIdx] = HLS;
        }

        HLinkSyncsMtx.unlock();
    }

    // wait for target to exist
    if (DoHLink) {
        HLS->Lock.WaitIdle();
        AF->ListEntry.LinkTarget = HLinkSyncs[AF->ListEntry.FInfoIdx]->Name;
    }

    // create extracted file
    LiveFile *LF = new LiveFile (AF->ListEntry,
                                 AF->Chunks, ChunkBlocks,
                                 DirAttribs, &DirAttribsMtx,
                                 DoHLink);

    // allow links to first file
    if (!DoHLink && HLS) {
        HLS->Name = LF->Name;
        HLS->Lock.PostIdle();
    }

    delete LF;
    delete AF;
}

void ArchiveRead::DoExtract () {
    // open the file list
    auto ListFile = OpenReadStream (ListPath);

    // parse and extract all the entries in the list
    u64 LineNo = 0;
    string ListLine;
    while (getline (ListFile, ListLine)) {
        LineNo ++;

        if (O.NumThreads) {
            JobCtrl *Job = ThreadPool.AllocThread();
            Job->JobType = JobCtrl::ExtractFile;
            Job->ExtractFileInfo.Arch     = this;
            Job->ExtractFileInfo.ListLine = ListLine;
            Job->ExtractFileInfo.LineNo   = LineNo;
            Job->Go();
        } else {
            DoExtractJob (ListLine, LineNo);
        }
    }

    // wait for all jobs to finish
    ThreadPool.WaitIdle();

    // handle deferred modification times
    for (auto& DirAttrib : DirAttribs) {
        SetOwn     (DirAttrib.Name, DirAttrib.Uid, DirAttrib.Gid );
        SetMode    (DirAttrib.Name, DirAttrib.Mode               );
        SetModTime (DirAttrib.Name, NsToTimeSpec(DirAttrib.MTime));
    }

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

void ArchiveCreate::PushListEntry (const FileListEntry &ListEntry) {
    stringstream SListLine;
    SListLine <<                                   ListEntry.Name           << ListRecSep;
    SListLine << "mode:"  << hex <<                ListEntry.Stats.st_mode  << " ";
    SListLine << "uid:"   << hex <<                ListEntry.Stats.st_uid   << " ";
    SListLine << "gid:"   << hex <<                ListEntry.Stats.st_gid   << " ";
    SListLine << "size:"  << dec <<                ListEntry.Stats.st_size  << " "; // note: decimal
    SListLine << "mtime:" << hex << TimeSpec_ToNs (ListEntry.Stats.st_mtim)       ;
    if (S_ISREG(ListEntry.Stats.st_mode))
        SListLine << " " << ListEntry.CompFlag << ":" << dec << ListEntry.FInfoIdx;
    if (S_ISLNK(ListEntry.Stats.st_mode))
        SListLine << ListRecSep << "slink:" << ListEntry.LinkTarget;
    SListLine << endl;

    // prevent corruption when multiple threads are creating file entries
    static mutex LocalMtx;
    LocalMtx.lock();

    ListFile << SListLine.str();

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

    // grab data block info
    if (ListEntry.FInfoIdx >= 0) {
        // extract information from the FInfo block
        string FInfoPacked;
        Arch->FInfoBlocks->SlurpBlock (ListEntry.FInfoIdx, FInfoPacked);

        // decompress
        string *SelData = &FInfoPacked;
        string  DeCompressed;
        if (ListEntry.CompFlag != CompFlagUnComp) {
            Comp::DeCompress (Comp::CompFlag2CompType (ListEntry.CompFlag, O), FInfoPacked, DeCompressed);
            SelData = &DeCompressed;
        }

        // parse finfo
        vecstr FInfoLines = SplitStr (*SelData, "\n");
        for (auto Line : FInfoLines) {
            if (  Line.size() < 3
              || (Line[0] != CompFlagUnComp &&
                  Line[0] != CompFlagComp
                 )
              ||  Line[1] != '-'
               )
                THROW_PBEXCEPTION_FMT ("Illegal FInfo format: %s", Line.c_str());
            char RecType = Line[0];
            Line.erase (0,2);
            vecstr Parts = SplitStr (Line, " ");
            Chunks.emplace_back (Comp::CompFlag2CompType (RecType, O),
                                 stoull (Parts[0].c_str()),
                                 Parts[1]);
        }
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
                                        ,const ChunkInfo *RefChunkInfo, BlockList *RefBlockList
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
//DBG ("CreateJob Name=%s\n", Name.c_str());
    ArchiveReference *RefArch = ((ArchiveCreate*)Arch)->ArchRef;
    ArchFileRead     *RF = NULL;
    FileListEntry     RefFileEntry;
//    if (RefArch && RefArch->FileMap.count (Name)) {
//        RefFileEntry = RefArch->FileMap[Name];
//
//        // grab info from the reference archive
//        RF = new ArchFileRead ((ArchiveRead *)Arch, RefFileEntry);
////DBG ("CreateJob Name Match\n");
////DBG ("LF size = %ld  RF size = %ld\n", LF->Stats.st_size, RF->Stats.st_size);
//    }
//
//    // if the reference is too different,
//    // eliminate linked copy in new archive
//    if (RF && (
//         (                 (LF->Stats.st_mode != RF->Stats.st_mode))
//      || (LF->IsFile()  && (LF->Stats.st_size != RF->Stats.st_size))
//      || (LF->IsSLink() && (LF->LinkTarget    != RF->LinkTarget   ))
//       )) {
//
////DBG ("Unlinking\n");
//        // eliminate hard-linked chunk blocks
//        for (auto &Chunk : RF->Chunks) {
////DBG ("Unlinking Chunk %ld\n", Chunk.Idx);
//            Arch->ChunkBlocks->UnLink (Chunk.Idx);
//        }
//
//        // eliminate the FInfo block
//        Arch->FInfoBlocks->UnLink (RefFileEntry.BlkNum);
//
//        // eliminate the File from the reference filemap
//        RefArch->FileMapMtx.lock();
//        RefArch->FileMap   .erase (Name);
//        RefArch->FileMapMtx.unlock();
//
//        delete RF;
//        RF = NULL;
//    }

    // fill in file list entry
    ListEntry.Name       = Name;
    ListEntry.Stats      = LF->Stats;
    ListEntry.CompFlag   = CompFlagUnComp;
    ListEntry.LinkTarget = LF->LinkTarget;

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
                Thr->CompressChunkInfo.ChunkData    = ChunkData;
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

        string FInfo;
        for (auto Return : Returns) {
            // wait for the job to complete
            Return->BL.WaitIdle();

            // add chunk to finfo
            FInfo += string("") + Return->CompFlag + "-" + to_string (Return->BlockIdx) + " " + Return->HashHex + "\n";

            delete Return;
        }

        // compress the finfo
        // if compression doesn't help, keep it uncompressed
        string *SelFInfo   = &FInfo;
        string Compressed;
        if (O.CompType != CompType_NONE) {
            Comp::Compress (FInfo, Compressed);
            if (Compressed.size() < FInfo.size()) {
                SelFInfo    = &Compressed;
                ListEntry.CompFlag = CompFlagComp;
            }
        }

        // Put the finfo into the archive
        ListEntry.FInfoIdx = Arch->FInfoBlocks->SpitNewBlock (*SelFInfo);
    }

    // update file list
    ((ArchiveCreate*)Arch)->PushListEntry (ListEntry);

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
    ((ArchiveCreate*)Arch)->PushListEntry (ListEntry);

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
