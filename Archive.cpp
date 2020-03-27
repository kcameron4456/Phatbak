#include "Archive.h"
#include "Logging.h"
#include "Utils.h"
#include "ThreadPool.h"
#include "Comp.h"
using namespace Utils;

#include <string>
#include <fstream>
#include <filesystem>
#include <queue>
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

    // initialize block allocators
    FInfoBlocks = new BlockList (FinfoDirPath);
    ChunkBlocks = new BlockList (ChunkDirPath);
}

Archive::~Archive() {
    DBGDTOR;

    delete FInfoBlocks;
    delete ChunkBlocks;

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
    Res.FInfoIdx      = INT64_MIN; // most negative possible
    Res.LineNo        = LineNo;

    // separate fields of rhs
    vecstr RHSToks = SplitStr (FirstCut[1], " ");
    for (auto &RHSTok : RHSToks) {
        vecstr Toks = SplitStr (RHSTok, ">");
        string &Name = Toks[0];
        string &Val  = Toks[1];
             if (Name == "mode" ) Res.Stats.st_mode =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "uid"  ) Res.Stats.st_uid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "gid"  ) Res.Stats.st_gid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "size" ) Res.Stats.st_size =               strtoull (Val.c_str(), NULL, 10);
        else if (Name == "mtime") Res.Stats.st_mtim = NsToTimeSpec (strtoull (Val.c_str(), NULL, 16));
        else if (Name == "U" || Name == "C") {
                                  Res.FInfoIdx      =               strtoull (Val.c_str(), NULL, 10);
                                  Res.CompFlag      =               Name[0];
                                  }
        else if (Name == "acl")  Res.Acl            =                         Val.c_str();
        else
            THROW_PBEXCEPTION_FMT ("Illegal entry in %s:%llu : %s", ListPath.c_str(), LineNo, RHSTok.c_str());
    }

    // parse optional third field
    // only slink
    if (FirstCut.size() == 3) {
        if (FirstCut[2].substr(0,6) != "slink>")
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
        THROW_PBEXCEPTION_FMT ("%s doesn't exist");
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

             if (OptName == "BlockNumModulus") O.BlockNumModulus = stoull               (OptVal);
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
    if (AF->ListEntry.FInfoIdx != INT64_MIN) {
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
    LF = NULL;

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

        function <void()> Task = [=](){DoExtractJob (ListLine, LineNo);};
        ThreadPool.Execute (Task);
    }

    // wait for all jobs to finish
    ThreadPool.WaitIdle();
fprintf (stderr, "file extract complete\n");

    // handle deferred modification times
    for (auto& DirAttrib : DirAttribs) {
        SetOwn      (DirAttrib.Name, DirAttrib.Uid, DirAttrib.Gid );
        SetMode     (DirAttrib.Name, DirAttrib.Mode);
        //SetFileAcls (DirAttrib.Name, DirAttrib.Acl);
        SetModTime  (DirAttrib.Name, NsToTimeSpec(DirAttrib.MTime));
    }
fprintf (stderr, "dir attribs complete\n");

    ListFile.close();
}

//////////////////////////////////////////////////////////////////////
ArchiveBase::ArchiveBase (RepoInfo *repo, const string &name) : ArchiveRead (repo, name) {
    // create a list of files with first-order info
    fstream FL = OpenReadStream (ListPath);
    string Line;
    u64 LineCount = 1;
    while (getline (FL, Line)) {
        FileListEntry FLE = ParseListLine (Line, LineCount);
        FileMap [FLE.Name] = FLE;
        LineCount ++;
    }
    FL.close();
fprintf (stderr, "ArchiveBase::ArchiveBase listed %ld files\n", (long)FileMap.size());
}

ArchiveBase::~ArchiveBase () {
}

//////////////////////////////////////////////////////////////////////
ArchiveCreate::ArchiveCreate (RepoInfo *repo, const string &name, ArchiveBase *base) : Archive (repo, name) {
    DBGCTOR;
    ZeroLenIdx = -1;
    ArchBase    = base;

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

    // if using a base arch, preload finfo and chunk allocators based on previous files
    if (ArchBase) {
fprintf (stderr, "ArchiveBase::ArchiveBase reverse allocation begun\n");
        // initialize the finfo and chunk blocklist allocator based on base files

        FInfoBlocks->ReverseAlloc(ArchBase->FInfoBlocks->TopDir);
        ChunkBlocks->ReverseAlloc(ArchBase->ChunkBlocks->TopDir);

        ThreadPool.WaitIdle();
fprintf (stderr, "ArchiveBase::ArchiveBase reverse allocation complete\n");
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
}

void ArchiveCreate::PushListEntry (const FileListEntry &ListEntry) {
    stringstream SListLine;
    SListLine <<                                   ListEntry.Name           << ListRecSep;
    SListLine << "mode>"  << hex <<                ListEntry.Stats.st_mode  << " ";
    SListLine << "uid>"   << hex <<                ListEntry.Stats.st_uid   << " ";
    SListLine << "gid>"   << hex <<                ListEntry.Stats.st_gid   << " ";
    SListLine << "size>"  << dec <<                ListEntry.Stats.st_size  << " "; // note: decimal
    SListLine << "mtime>" << hex << TimeSpec_ToNs (ListEntry.Stats.st_mtim)       ;
    if (ListEntry.Acl.size())
        SListLine << " acl>" << ListEntry.Acl;
    if (ListEntry.FInfoIdx != INT64_MIN)
        SListLine << " " << ListEntry.CompFlag << ">" << dec << ListEntry.FInfoIdx;
    if (S_ISLNK(ListEntry.Stats.st_mode))
        SListLine << ListRecSep << "slink>" << ListEntry.LinkTarget;
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
        stringstream ss (*SelData);
        string Line;
        while (getline (ss, Line)) {
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
            if (Parts.size() != 2)
                THROW_PBEXCEPTION_FMT ("Illegal FInfo format: %s", Line.c_str());
            Chunks.emplace_back (RecType,
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
    Arch   = arch;
    LF     = lf;
    Name   = LF->Name;

    Mtx.lock();
}

ArchFileCreate::~ArchFileCreate () {
    if (LF)
        delete LF;
}

void ArchFileCreate::HashAndCompressJob (const string &ChunkData
                                        ,const ChunkInfo *BaseChunkInfo, const BlockList *BaseChunkBlocks
                                        ,HashAndCompressReturn *HACR) {
    // compute hash
    Hash Hasher (O.HashType);
    HACR->Hash = Hasher.HashStr(ChunkData);

    // compare to base hash
    HACR->Keep = 0;
    if (BaseChunkInfo && HACR->Hash == BaseChunkInfo->Hash) {
        // keep cloned chunk
        HACR->CompFlag = BaseChunkInfo->CompFlag;
        HACR->BlockIdx = BaseChunkInfo->ChunkIdx;
        HACR->Keep     = 1;

        // link to base file
        Arch->ChunkBlocks->Link (BaseChunkInfo->ChunkIdx, BaseChunkBlocks->TopDir);
    } else {
        // create fresh chunk

        // need to free if marked allocated
        if (BaseChunkInfo)
            Arch->ChunkBlocks->Free (BaseChunkInfo->ChunkIdx);

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

void ArchFileCreate::CreateJob (bool KeepAF) {
    // fill in the file list entry
    ListEntry.Name       = Name;
    ListEntry.Stats      = LF->Stats;
    ListEntry.CompFlag   = CompFlagUnComp;
    ListEntry.LinkTarget = LF->LinkTarget;
    ListEntry.FInfoIdx   = INT64_MIN;
    ListEntry.LineNo     = 0;

    // for regular files, either create finfo and chunks or keep cloned base values
    if (LF->IsFile() && ListEntry.Stats.st_size > 0) {
        ArchiveBase   *BaseArchive = Arch->ArchBase;
        FileListEntry  BaseFileEntry;
        ArchFileRead  *BaseFile        = NULL;
        BlockList     *BaseChunkBlocks = NULL;
        bool           DoFileRead      = true;
        if (BaseArchive) {
            bool UseBase = false;
            BaseArchive->FileMapMtx.lock();
            if (BaseArchive->FileMap.count (Name)) {
                BaseFileEntry = BaseArchive->FileMap[Name];
                UseBase       = true;
            }
            BaseArchive->FileMapMtx.unlock();
            if (!UseBase)
                BaseArchive = NULL;
        }
        if (BaseArchive) {
            ListEntry.FInfoIdx = BaseFileEntry.FInfoIdx;
            ListEntry.CompFlag = BaseFileEntry.CompFlag;
            BaseFile           = new ArchFileRead (BaseArchive, BaseFileEntry);
            BaseChunkBlocks    = BaseArchive->ChunkBlocks;

            // if the file stats aren't too different, we don't have to compare file contents
            DoFileRead =            ListEntry.Stats.st_size != BaseFileEntry.Stats.st_size
                || !TimeSpecsEqual (ListEntry.Stats.st_mtim  , BaseFileEntry.Stats.st_mtim);
        }

        string FInfo;
        bool   KeepBaseFinfo = true;

        if (DoFileRead) {
            // actually read data from live file

            // async return values from hash and compress
            queue <HashAndCompressReturn *> Returns;
            function <void(bool)> CheckReturns = [&](bool Wait) {
                // process the job return vals
                while (Returns.size()) {
                    auto Return = Returns.front();

                    // wait for idle or abort
                    if (!Wait && Return->BL.CheckBusy())
                        break;
                    Return->BL.WaitIdle();

                    // add chunk to finfo
                    FInfo += string("") +            Return->CompFlag
                             + "-"      + to_string (Return->BlockIdx)
                             + " "      +            Return->Hash
                             + "\n";

                    // remember if the finfo changes
                    KeepBaseFinfo &= Return->Keep;

                    delete Return;
                    Returns.pop();
                }
            };

            // read chunk from live file
            string ChunkData;
            u32    ChunkIdx = 0;
            LF->OpenRead();
            while (LF->ReadChunk (ChunkData)) {
                HashAndCompressReturn *Return = new HashAndCompressReturn;
                Returns.push (Return);

                ChunkInfo *BaseChunkInfo = BaseArchive && (ChunkIdx < BaseFile->Chunks.size()) ?
                                           &BaseFile->Chunks[ChunkIdx] : NULL;

                // read, compress, and test the data
                function <void()> Task = [=]() {
                    HashAndCompressJob (ChunkData, BaseChunkInfo, BaseChunkBlocks, Return);
                };
                ThreadPool.Execute (Task, 0);

                // process and returns that are ready
                CheckReturns (0);

                ChunkIdx++;
            }
            LF->Close();

            // process the job return vals
            CheckReturns (1);

            // done with base file
            if (BaseFile)
                delete BaseFile;
        } else {
            // just link the chunks to base archive
            for (auto &ChunkInfo : BaseFile->Chunks)
                Arch->ChunkBlocks->Link (ChunkInfo.ChunkIdx, BaseArchive->ChunkBlocks->TopDir);
            KeepBaseFinfo = true;
        }

        if (KeepBaseFinfo) {
            // just link to base archive version
            if (ListEntry.FInfoIdx >= 0)
                Arch->FInfoBlocks->Link (ListEntry.FInfoIdx, BaseArchive->FInfoBlocks->TopDir);
        } else {
            // create new FInfo block
            if (ListEntry.FInfoIdx >= 0)
                Arch->FInfoBlocks->Free (ListEntry.FInfoIdx);

            // compress it
            // if compression doesn't help, keep it uncompressed
            string *SelFInfo   = &FInfo;
            string Compressed;
            if (O.CompType != CompType_NONE) {
                Comp::Compress (FInfo, Compressed);
                if (Compressed.size() < FInfo.size()) {
                    SelFInfo           = &Compressed;
                    ListEntry.CompFlag =  CompFlagComp;
                }
            }

            // Put the finfo into the archive
            ListEntry.FInfoIdx = Arch->FInfoBlocks->SpitNewBlock (*SelFInfo);
        }
    }

    // use negative FInfoIdx to designate potentially hardlinked zero-size files (or pipes, etc)
    if (LF->Stats.st_size == 0 && !LF->IsDir() && !LF->IsSLink()) {
        Arch->ZeroLenIdxMtx.lock();

        ListEntry.FInfoIdx = --Arch->ZeroLenIdx;

        Arch->ZeroLenIdxMtx.unlock();
    }

    // add acl to finfo
    ListEntry.Acl = GetFileAcls (Name);

    // update file list
    Arch->PushListEntry (ListEntry);

    // flag completion
    Mtx.unlock();

    // delete this if we know it won't be needed again
    if (!KeepAF)
        delete this;
}

void ArchFileCreate::Create (bool Keep) {
    function <void()> Task = [=](){CreateJob (Keep);};
    ThreadPool.Execute (Task);
}

// link to previously archived file
void ArchFileCreate::CreateLink (ArchFileCreate *Prev) {
    // wait for processing of original file to complete
    Prev->Mtx.lock();

    ListEntry      = Prev->ListEntry;
    ListEntry.Name = Name;
    Arch->PushListEntry (ListEntry);

    // release prev file
    Prev->Mtx.unlock();

    // release this file (even though nobody will be waiting for this one)
    Mtx.unlock();

    // this archive file won't be used again
    delete this;
}

//////////////////////////////////////////////////////////////////////
