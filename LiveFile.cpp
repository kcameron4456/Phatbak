#include "LiveFile.h"
#include "Logging.h"
#include "Opts.h"
#include "Utils.h"
using namespace Utils;

#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
using namespace std;

LiveFile::LiveFile (const string &name) { // for create
    Name = name;

    FD = -1;

    // get file info
    if (lstat (Name.c_str(), &Stats) < 0)
        THROW_PBEXCEPTION_IO ("Can't stat file: %s", Name.c_str());

    // type-specific actions
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

LiveFile::LiveFile (const string &name        , const string &stats   , const string &ltarg,
                    vector <ChunkInfo> &Chunks, BlockList *ChunkBlocks) { // for extract, etc
    Name = name;
    ImportInfoHeader (stats);
    LinkTarget = ltarg;

    // if extracting, create the file now
    if (O.Operation == Opts::DoExtract) {
        Name.insert (0, O.ExtractTarget);

printf ("Name=%s stats=%s\n", Name.c_str(), stats.c_str());
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
            // not yet - might be complicated
        } else if (IsFile()) {
            FILE *F = OpenWriteBin (Name);
            for (auto Chunk: Chunks) {
                string ChunkData;
                ChunkBlocks->SlurpBlock (Chunk.Idx, ChunkData);

                // TBD: handle decompress

                string ChunkDataHash = HashStr (Chunk.HashType, ChunkData);
                if (ChunkDataHash != Chunk.Hash)
                    THROW_PBEXCEPTION_FMT ("Hash mismatch on data chunk #%llu", Chunk.Idx);

                WriteBinary (F, ChunkData);
            }
            fclose (F);
        }
    }
}

vector <string> LiveFile::GetSubs () {
    vector <string> Subs;
    if (!IsDir())
        return Subs;
    for (const auto& entry : filesystem::directory_iterator(Name))
        Subs.push_back (Name + "/" + entry.path().filename().string());
    return Subs;
}

string LiveFile::MakeInfoHeader () const {
    stringstream res;
    res << "mode:"  << hex << Stats.st_mode << " ";
    res << "uid:"   << hex << Stats.st_uid  << " ";
    res << "gid:"   << hex << Stats.st_gid  << " ";
    res << "size:"  <<        Stats.st_size << " "; // keep size in decimal to make it easier to read and debug
    res << "mtime:" << hex << mTime()       << " ";

    return res.str();
}

void LiveFile::ImportInfoHeader (const string &Hdr) {
    vector <string> Fields = SplitStr (Hdr, " ");
    for (auto Field : Fields) {
        vector <string> Two = SplitStr (Field, ":");
        if (Two.size() != 2)
            THROW_PBEXCEPTION_FMT ("Illegal header field : %s", Field.c_str());
        string &Name = Two[0];
        string &Val  = Two[1];
             if (Name == "mode" ) Stats.st_mode = strtoull (Val.c_str(), NULL, 16);
        else if (Name == "uid"  ) Stats.st_uid  = strtoull (Val.c_str(), NULL, 16);
        else if (Name == "gid"  ) Stats.st_gid  = strtoull (Val.c_str(), NULL, 16);
        else if (Name == "size" ) Stats.st_size = strtoull (Val.c_str(), NULL, 10);
        else if (Name == "mtime") {
            uint64_t ns           = strtoull (Val.c_str(), NULL, 16);
            Stats.st_mtime        = ns / 1000000000Ull;
            Stats.st_mtim.tv_nsec = ns % 1000000000Ull;
        }
    }
}

void SplitFileName (const string &RawName, string &Path, string &Name) {
    // split into path and leaf names
    // leaf is just the part after the last "/"
    int LastSlash = RawName.rfind ('/');
    Path = RawName.substr (0,LastSlash);
    Name = RawName.substr (LastSlash+1);
}

void LiveFile::Close () {
    if (close (FD) < 0)
         THROW_PBEXCEPTION_IO ("Can't close file: %s ", Name.c_str());
    FD = -1;
}

void LiveFile::OpenRead () {
    if ((FD = open (Name.c_str(), O_RDONLY)) < 0)
         THROW_PBEXCEPTION_IO ("Can't open file for read: %s ", Name.c_str());
}

void LiveFile::OpenWrite () {
    if ((FD = open (Name.c_str(), O_WRONLY)) < 0)
         THROW_PBEXCEPTION_IO ("Can't open file for write: %s ", Name.c_str());
}

int LiveFile::Read (char *Buf, int ReqSize) {
    int RdSize;
    if ((RdSize = read (FD, Buf, ReqSize)) < 0)
         THROW_PBEXCEPTION_IO ("Error reading from: " + Name);
    return RdSize;
}

int LiveFile::ReadChunk (char *Buf) {
    return Read (Buf, O.ChunkSize);
}
