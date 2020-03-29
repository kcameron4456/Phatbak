#include "LiveFile.h"
#include "Logging.h"
#include "Opts.h"
#include "Utils.h"
#include "Comp.h"
#include "ThreadPool.h"
using namespace Utils;

#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

// for create
LiveFile::LiveFile (const string &name) {
    DBGCTOR;
    Name = name;
    F = NULL;

    // get file info
    if (lstat (Name.c_str(), &Stats) < 0)
        THROW_PBEXCEPTION_IO ("Can't stat file: %s", Name.c_str());

    // type-specific actions
    LinkTarget = "";
           if (IsFile  ()) {
    } else if (IsDir   ()) {
    } else if (IsFifo  ()) {
    } else if (IsSocket()) {
    } else if (IsSLink ()) {
        char Targ [1000];
        int TargSize = readlink (Name.c_str(), Targ, sizeof(Targ));
        if (TargSize < 0)
            THROW_PBEXCEPTION_IO ("Can't read symbolic link '%s'", Name.c_str());
        if (TargSize >= 1000)
            THROW_PBEXCEPTION ("Symbolic link '%s' too long", Name.c_str());
        LinkTarget = string (Targ, TargSize);
    } else {
        THROW_PBEXCEPTION ("File %s is an unsupported type", Name.c_str());
    }
}

void ExtractChunkJob (const ChunkInfo *Chunk, const BlockList *ChunkBlocks, i64 BlockIdx, FILE *F, BusyLock *Lock, BusyLock *PrevLock) {
    string ChunkData;
    ChunkBlocks->SlurpBlock (BlockIdx, ChunkData);

    // handle decompress
    string *SelData = &ChunkData;
    string DeCompressed;
    if (Chunk->CompFlag != CompFlagUnComp) {
        Comp::DeCompress (Chunk->CompFlag, ChunkData, DeCompressed);
        SelData = &DeCompressed;
    }

    string ChunkDataHash = HashStr (O.HashType, *SelData);
    if (ChunkDataHash != Chunk->Hash)
        THROW_PBEXCEPTION_FMT ("Hash mismatch on data chunk #%llu", Chunk->ChunkIdx);

    if (PrevLock)
        PrevLock->WaitIdle();

    WriteBinary (F, *SelData);

    Lock->PostIdle();
}

// for extract, etc
LiveFile::LiveFile (const FileListEntry &ListEntry
                   ,const vector <ChunkInfo> &Chunks , const BlockList *ChunkBlocks
                   ,vector <DirAttribRec> &DirAttribs, mutex *DirAttribsMtx
                   ,bool DoHLink
                   ) {
    DBGCTOR;
    Name       = ListEntry.Name;
    Stats      = ListEntry.Stats;
    LinkTarget = ListEntry.LinkTarget;
    F          = NULL;

    // if extracting, create the file now
    if (O.Operation == Opts::DoExtract) {
        Name.insert (0, O.ExtractTarget);

        // break name into parts then create the directory containing the name
        vecstr Parts = SplitStr (Name, "/");
        Parts.pop_back();
        string Parent = JoinStrs (Parts, "/");
        CreateDir (Parent, 1);

        // create hard link
        if (DoHLink) {
            MakeHardLink (LinkTarget, Name);
            return;
        }

        // create whatever type of thing it is
        if (IsDir()) {
            CreateDir (Name, 1);
        } else if (IsSLink()) {
            if (symlink (LinkTarget.c_str(), Name.c_str()) < 0)
                THROW_PBEXCEPTION_IO ("Can't create symbolic link: %s", Name.c_str());
        } else if (IsFifo()) {
            if (mkfifo (Name.c_str(), Stats.st_mode) < 0)
                THROW_PBEXCEPTION_IO ("Can't create fifo: %s", Name.c_str());
        } else if (IsSocket()) {
            CreateSocket (Name);
        } else if (IsFile()) {
            vector <BusyLock *> Locks;
            BusyLock *PrevLock = NULL;

            FILE *F = OpenWriteBin (Name);
            for (auto ChunkItr = Chunks.begin(); ChunkItr != Chunks.end(); ChunkItr++) {
                const ChunkInfo &Chunk = *ChunkItr;

                BusyLock *Lock = new BusyLock(1);
                Locks.push_back(Lock);

                // get help on all but the final chunk
                // avoid deadlock if NumThreads==1 (because this function uses a thread)
                if (O.NumThreads > 1 && ChunkItr != (Chunks.end()-1)) {
                    function <void()> Task = [=]() {
                        ExtractChunkJob (&Chunk, ChunkBlocks, Chunk.ChunkIdx, F, Lock, PrevLock);
                    };
                    ThreadPool.Execute (Task);
                } else {
                    ExtractChunkJob (&Chunk, ChunkBlocks, Chunk.ChunkIdx, F, Lock, PrevLock);
                }


                PrevLock = Lock;
            }

            // wait for helpers to finish
            for (auto Lock : Locks) {
                Lock->WaitIdle();
                delete Lock;
            }

            fclose (F);
        }

        // set attributes

        // must defer directory attributes setting until
        // all files have been extracted
        if (IsDir()) {
            // dir modification time needs to be set later
            DirAttribRec DirAttrib;
            DirAttrib.Name  = Name;
            DirAttrib.Uid   = Stats.st_uid;
            DirAttrib.Gid   = Stats.st_gid;
            DirAttrib.Mode  = Stats.st_mode;
            DirAttrib.MTime = TimeSpecToNs(Stats.st_mtim);
            DirAttrib.Acl   = ListEntry.Acl;

            DirAttribsMtx->lock();

            DirAttribs.push_back(DirAttrib);

            DirAttribsMtx->unlock();
        } else {
            // set directory user and group
            SetOwn (Name, Stats.st_uid, Stats.st_gid, IsSLink());

            // set mode
            if (!IsSLink()) {
                SetMode (Name, Stats.st_mode);

                // set access control list
                SetFileAcls (Name, Stats.st_mode, ListEntry.Acl);
            }

            // set time
            SetModTime (Name, Stats.st_mtim);
        }
    }
}

LiveFile::~LiveFile () {
    DBGDTOR;
    Close();
}

vecstr LiveFile::GetSubs () {
    vecstr Subs;
    if (!IsDir())
        return Subs;
    for (const auto& entry : filesystem::directory_iterator(Name))
        Subs.push_back (Name + "/" + entry.path().filename().string());
    return Subs;
}

void SplitFileName (const string &RawName, string &Path, string &Name) {
    // split into path and leaf names
    // leaf is just the part after the last "/"
    int LastSlash = RawName.rfind ('/');
    Path = RawName.substr (0,LastSlash);
    Name = RawName.substr (LastSlash+1);
}

void LiveFile::OpenRead () {
    F = OpenReadBin (Name);
    assert (F);
}

void LiveFile::OpenWrite () {
    F = OpenWriteBin (Name);
    assert (F);
}

void LiveFile::Close () {
    if (F)
        fclose (F);
    F = NULL;
}

int LiveFile::Read (char *Buf, int ReqSize) {
    assert (F);
    return ReadBinary (F, Buf, ReqSize);
}

int  LiveFile::ReadChunk (string &Chunk) {
    assert (F);
    return ReadBinary (F, Chunk, O.ChunkSize);
}

void LiveFile::Write (const string &Str) {
    assert (F);
    WriteBinary (F, Str);
}

void LiveFile::Write (char *Buf, int ReqSize) {
    assert (F);
    WriteBinary (F, Buf, ReqSize);
}
