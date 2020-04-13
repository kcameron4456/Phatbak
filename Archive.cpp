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
    Repo           = repo;
    Name           = name;

    assert (Name != "");

    ArchDirPath    = Repo->Name  + "/" + Name;
    IDPath         = ArchDirPath + "/" + PHATBAK_ARCH_ID;
    FinishedPath   = ArchDirPath + "/" + PHATBAK_ARCH_FINISHED;
    ListPath       = ArchDirPath + "/List";
    LogPath        = ArchDirPath + "/PhatBak.log";
    OptionsPath    = ArchDirPath + "/Options";
    FinfoDirPath   = ArchDirPath + "/FInfo";
    ChunkDirPath   = ArchDirPath + "/Chunks";
    ExtraDirPath   = ArchDirPath + "/Extra";

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
    // only slink allowed
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

    if (!fs::is_directory (ArchDirPath))
        ERROR ("Archive %s is not a directory\n", ArchDirPath.c_str());

    // see if it's really an archive
    if (!fs::exists (IDPath))
        ERROR ("%s doesn't exist\n", IDPath.c_str());

    // get options used in the archive
    ParseOptions ();

    // get ready to read file list
    ListFile = OpenReadStream (ListPath);
}

ArchiveRead::~ArchiveRead() {
    DBGDTOR;
    for (auto Itr : HLinkSyncs)
        delete Itr.second;
}

void ArchiveRead::ParseOptions () {
    // default to global options
    O = ::O;

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

             if (OptName == "FileArgs"       ) O.FileArgs        = SplitStr             (OptVal, " ");
        else if (OptName == "CWD"            ) O.CWD             =                      (OptVal);
        else if (OptName == "BlockNumModulus") O.BlockNumModulus = stoull               (OptVal);
        else if (OptName == "ChunkSize"      ) O.ChunkSize       = stoull               (OptVal);
        else if (OptName == "HashType"       ) O.HashType        = HashNameToEnum       (OptVal);
        else if (OptName == "CompType"       ) O.CompType        = Comp::CompNameToEnum (OptVal);
        else if (OptName == "CompLevel"      ) O.CompLevel       = stoull               (OptVal);
    }

    OptsFile.close();

    // apply modified options to global
    // TBD: make sure everyone is using the correct options then get rid of this
    ::O = O;
}

void ArchiveRead::DoExtractJob (const string &ListLine, u64 LineNo) {
    FileListEntry ListEntry = ParseListLine (ListLine, LineNo);

    // filter against user-specified extraction list
    if (O.FileArgs.size()) {
        bool FileOk = false;
        for (auto FileArg: O.FileArgs) {
            string CanFileArg = CanonizeFileName (FileArg, O.CWD);
            FileOk |= ListEntry.Name.find (CanFileArg, 0) == 0;
        }
        if (!FileOk)
            return;
    }

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
    // parse and extract all the entries in the list
    u64 LineNo = 0;
    string ListLine;
    while (getline (ListFile, ListLine)) {
        LineNo ++;

        function <void()> Task = [=, this](){DoExtractJob (ListLine, LineNo);};
        ThreadPool.Execute (Task);
    }

    // wait for all jobs to finish
    ThreadPool.WaitIdle();

    // handle deferred modification times for directories
    // first sort so we can start with leaf nodes
    sort (DirAttribs.begin(), DirAttribs.end(),
          [](DirAttribRec &A, DirAttribRec &B)->bool {return A.Name.size() > B.Name.size();});
    for (auto& DirAttrib : DirAttribs) {
        SetOwn      (DirAttrib.Name, DirAttrib.Uid, DirAttrib.Gid );
        SetMode     (DirAttrib.Name, DirAttrib.Mode);
        SetFileAcls (DirAttrib.Name, DirAttrib.Mode, DirAttrib.Acl);
        SetModTime  (DirAttrib.Name, NsToTimeSpec(DirAttrib.MTime));
    }
}

void ArchiveRead::DoList () {
    // create a list of files with first-order info
    string Line;
    u64 LineCount = 0;
    while (getline (ListFile, Line)) {
        LineCount ++;

        FileListEntry FLE = ParseListLine (Line, LineCount);
        printf ("%s\n", FLE.Name.c_str());
    }
}

static void FindBlockFiles (const string &Dir, const string &TopDir, map <i64, bool> &BlockMap, mutex &BlockMapMtx) {
    vecstr SubDirs, SubFiles;
    SlurpDir (Dir, SubDirs, SubFiles);
    for (auto File: SubFiles) {
        // check file name
        if (File.find_first_not_of ("0123456789") != string::npos)
            WARN ("Unexpected file: %s\n", (Dir + "/" + File).c_str());
        i64 BlockIdx = strtoll (File.c_str(), NULL, 10);

        // remember this block
        BlockMapMtx.lock();
        if (BlockMap.count (BlockIdx))
            WARN ("Block #%ld seen twice in %s\n", BlockIdx, TopDir.c_str());
        BlockMap [BlockIdx] = 1;
        BlockMapMtx.unlock();
    }

    // do subdirs
    for (auto SubDir: SubDirs) {
        function <void()> Task = [=,&BlockMap,&BlockMapMtx]() {
            FindBlockFiles (Dir + "/" + SubDir, TopDir, BlockMap, BlockMapMtx);
        };
        ThreadPool.Execute (Task);
    }
}

void ArchiveRead::DoTestJob (const string ListLine, u64 LineCount
                            ,map <i64, bool> &FInfosMap, map <i64, bool> &ChunksMap
                            ,mutex &FInfosMapMtx, mutex &ChunksMapMtx
                            ) {

    FileListEntry ListEntry = ParseListLine (ListLine, LineCount);
    if (O.ShowFiles)
        printf ("%s\n", ListEntry.Name.c_str());
    if (ListEntry.FInfoIdx < 0)
        return;

    FInfosMapMtx.lock();
    bool DoFileCheck = FInfosMap.count(ListEntry.FInfoIdx) == 0;
    FInfosMap[ListEntry.FInfoIdx] = 1;
    FInfosMapMtx.unlock();
    if (!DoFileCheck)
        return; // another thread has done (or is doing) the check

    auto AF = new ArchFileRead (this, ListEntry);
    for (auto Chunk : AF->Chunks) {
        ChunksMapMtx.lock();
        ChunksMap[Chunk.ChunkIdx] = 1;
        ChunksMapMtx.unlock();

        function <void()> Task = [=,this]() {
            // grab the chunk
            string ChunkData;
            ChunkBlocks->SlurpBlock (Chunk.ChunkIdx, ChunkData);

            // handle decompress
            string *SelData = &ChunkData;
            string DeCompressed;
            if (Chunk.CompFlag != CompFlagUnComp) {
                Comp::DeCompress (Chunk.CompFlag, ChunkData, DeCompressed);
                SelData = &DeCompressed;
            }

            // check hash
            string ChunkDataHash = HashStr (O.HashType, *SelData);
            if (ChunkDataHash != Chunk.Hash)
                WARN ("Hash mismatch on data chunk #%ld\n", Chunk.ChunkIdx);
        };
        ThreadPool.Execute (Task);
    }
    delete AF;
}

void ArchiveRead::DoTest () {
    // test all files in the archive
    // record all used finfo and chunk blocks
    map <i64, bool> UsedFInfosMap   , UsedChunksMap   ;
    mutex           UsedFInfosMapMtx, UsedChunksMapMtx;
    string Line;
    u64 LineCount = 0;
    while (getline (ListFile, Line)) {
        LineCount ++;
        function <void()> Task = [&,this,Line,LineCount]() {
            DoTestJob (Line, LineCount, UsedFInfosMap, UsedChunksMap, UsedFInfosMapMtx, UsedChunksMapMtx);
        };
        ThreadPool.Execute (Task);
    }
    ThreadPool.WaitIdle();

    // find existing block files
    map <i64, bool> FoundFInfosMap   , FoundChunksMap   ;
    mutex           FoundFInfosMapMtx, FoundChunksMapMtx;
    FindBlockFiles (FinfoDirPath, FinfoDirPath, FoundFInfosMap, FoundFInfosMapMtx);
    FindBlockFiles (ChunkDirPath, ChunkDirPath, FoundChunksMap, FoundChunksMapMtx);
    ThreadPool.WaitIdle();

    // make sure all finfo and chunk blocks are used
    for (auto Itr : FoundFInfosMap)
        if (!UsedFInfosMap.count(Itr.first))
            ERROR ("Unused FInfo block found: %ld\n", Itr.first);
    for (auto Itr : FoundChunksMap)
        if (!UsedChunksMap.count(Itr.first))
            ERROR ("Unused Chunk block found: %ld\n", Itr.first);
}

void ArchiveRead::DoCompareJob (const FileListEntry &ListEntry) {
    // stat the live file
    LiveFile *LF = new LiveFile (ListEntry.Name);

    // compare attibutes
    if (ListEntry.Stats.st_mode != LF->Stats.st_mode)
        WARN ("Archived mode (%o) doesn't match (%o) for file: %s\n", ListEntry.Stats.st_mode, LF->Stats.st_mode, ListEntry.Name.c_str());
    if (!TimeSpecsEqual (ListEntry.Stats.st_mtim, LF->Stats.st_mtim))
        WARN ("Archived modification time (%s) doesn't match (%s) for file: %s\n",
              TimeSpecToText(ListEntry.Stats.st_mtim).c_str(), TimeSpecToText (LF->Stats.st_mtim).c_str(), ListEntry.Name.c_str());

    // compare contents
    if (ListEntry.Stats.st_size == LF->Stats.st_size) {
        if (LF->IsFile()) {
            // open the file in the archive
            auto AF = new ArchFileRead (this, ListEntry);

            // open the live file for reading data
            LF->OpenRead();
            for (auto Chunk : AF->Chunks) { 
                // grab the chunk
                string ChunkData;
                ChunkBlocks->SlurpBlock (Chunk.ChunkIdx, ChunkData);

                // handle decompress
                string *SelData = &ChunkData;
                string DeCompressed;
                if (Chunk.CompFlag != CompFlagUnComp) {
                    Comp::DeCompress (Chunk.CompFlag, ChunkData, DeCompressed);
                    SelData = &DeCompressed;
                }

                // check hash
                string ChunkDataHash = HashStr (O.HashType, *SelData);
                if (ChunkDataHash != Chunk.Hash)
                    WARN ("Hash mismatch on data chunk #%ld\n", Chunk.ChunkIdx);

                // compare data
                string LFChunkData;
                if (!LF->ReadChunk (LFChunkData)) {
                    WARN ("Unexpected end of read data from: %s\n", ListEntry.Name.c_str());
                    break;
                }
                if (*SelData != LFChunkData) {
                    WARN ("Contents of archived file don't match: %s\n", ListEntry.Name.c_str());
                    break;
                }
            }
            LF->Close();

            delete AF;
        }
    } else {
        WARN ("Archived size (%ld) doesn't match (%ld) for file: %s\n", ListEntry.Stats.st_size, LF->Stats.st_size, ListEntry.Name.c_str());
    }

    delete LF;
}

void ArchiveRead::DoCompare () {
    // canonicalize archive dir names
    vecstr CanFileArgs;
    for (auto &FileArg: O.FileArgs)
        CanFileArgs.push_back (CanonizeFileName (FileArg, O.CWD));

    // compare all files in the archive
    string Line;
    u64 LineCount = 0;
    while (getline (ListFile, Line)) {
        LineCount ++;
        FileListEntry ListEntry = ParseListLine (Line, LineCount);

        // filter against file args used during the archive creation
        bool Keep = 0;
        for (auto FileArg : CanFileArgs)
            if (ListEntry.Name.find (FileArg) == 0)
                Keep = 1;
        if (!Keep)
            continue;

        if (O.ShowFiles)
            printf ("%s\n", ListEntry.Name.c_str());

        function <void()> Task = [=,this]() {
            DoCompareJob (ListEntry);
        };
        ThreadPool.Execute (Task, 0);
    };

    ThreadPool.WaitIdle();
}

//////////////////////////////////////////////////////////////////////
ArchiveBase::ArchiveBase (RepoInfo *repo, const string &name) : ArchiveRead (repo, name) {
    // create a list of files with first-order info
    string Line;
    u64 LineCount = 0;
    while (getline (ListFile, Line)) {
        LineCount ++;
        FileListEntry FLE = ParseListLine (Line, LineCount);
        FileMap [FLE.Name] = FLE;
    }
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
    LogFile << "Backup Started At: " << O.StartTimeTxt << endl;

    // create Options file
    O.ArchDirName = Name;
    if (ArchBase)
        O.BaseArchive = ArchBase->ArchDirPath;
    fstream OptFile = OpenWriteStream (OptionsPath);
    O.Print (OptFile);
    OptFile.close();

    // create new archive subdirs
    CreateDir (FinfoDirPath);
    CreateDir (ChunkDirPath);
    CreateDir (ExtraDirPath);

    // if using a base arch, preload finfo and chunk allocators based on previous files
    if (ArchBase) {
        // initialize the finfo and chunk blocklist allocator based on base files
        FInfoBlocks->ReverseAlloc(ArchBase->FinfoDirPath);
        ChunkBlocks->ReverseAlloc(ArchBase->ChunkDirPath);

        ThreadPool.WaitIdle();
    }

    // prepare the file list for write
    ListFile = OpenWriteStream (ListPath);
}

ArchiveCreate::~ArchiveCreate () {
    DBGDTOR;

    ThreadPool.WaitIdle ();

    u64 EndTime = TimeNowNs ();
    string EndTimeStr = NsToText (EndTime);

    LogFile << "Backup Ended At: " << EndTimeStr << endl;

    u64 NsDelta = EndTime - O.StartTime;

    LogFile.precision(2);
    LogFile << "Elasped Time: " << fixed << NsDelta/1e9 << " seconds\n";

    LogFile.close();

    Touch (FinishedPath);
}

void ArchiveCreate::PushListEntry (const FileListEntry &ListEntry) {
    stringstream SListLine;
    SListLine <<                                  ListEntry.Name           << ListRecSep;
    SListLine << "mode>"  << hex <<               ListEntry.Stats.st_mode  << " ";
    SListLine << "uid>"   << hex <<               ListEntry.Stats.st_uid   << " ";
    SListLine << "gid>"   << hex <<               ListEntry.Stats.st_gid   << " ";
    SListLine << "size>"  << dec <<               ListEntry.Stats.st_size  << " "; // note: decimal
    SListLine << "mtime>" << hex << TimeSpecToNs (ListEntry.Stats.st_mtim)       ;
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

void ArchFileCreate::Create (InodeInfo *Inode) {
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
                function <void()> Task = [=,this]() {
                    HashAndCompressJob (ChunkData, BaseChunkInfo, BaseChunkBlocks, Return);
                };
                ThreadPool.Execute (Task, 0);

                // process any returns that are ready
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
    if (!LF->IsSLink())
        ListEntry.Acl = GetFileAcls (Name, LF->Mode());

    // update file list
    Arch->PushListEntry (ListEntry);

    // update info for hard links
    if (Inode) {
        Inode->Mtx.lock();

        Inode->ListEntry = ListEntry;
        Inode->Complete  = true;

        // handle waiting links
        for (string &Link : Inode->Links) {
            ListEntry.Name = Link;
            Arch->PushListEntry (ListEntry);
        }
        Inode->Links.clear();

        Inode->Mtx.unlock();
    }

    // finished with this file
    delete this;
}

// link to previously archived file
void ArchFileCreate::CreateLink (InodeInfo *First) {
    First->Mtx.lock();
    if (First->Complete) {
        First->Mtx.unlock();
        FileListEntry ListEntry = First->ListEntry;
        ListEntry.Name = Name;
        Arch->PushListEntry (ListEntry);
    } else {
        First->Links.push_back (Name);
        First->Mtx.unlock();
    }

    // this file is done
    delete this;
}

//////////////////////////////////////////////////////////////////////
