#include "Utils.h"
#include "Logging.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filesystem>
namespace fs = std::filesystem;

vecstr Utils::SplitStr (string Src, const string &Pat) {
    vecstr Toks;
    while (Src.size()) {
        auto TokEnd = Src.find (Pat);
        string Tok = Src.substr (0, TokEnd);
        Toks.push_back(Tok);
        Src.erase (0, Tok.size() + Pat.size());
    }
    return Toks;
}

void Utils::TrimStr (string  *Str) {
    static const char *WhiteSpace = " \t\n";

    // strip leading whitespace
    size_t Begin = Str->find_first_not_of (WhiteSpace);
    Str->erase (0, Begin);

    // strip trailing whitespace
    size_t End = Str->find_last_not_of (WhiteSpace);
    Str->erase (End + 1);
}

string Utils::TrimStr (string Str) {
    Utils::TrimStr (&Str);
    return Str;
}

string Utils::CanonizeFileName (const string &RawName) {
    string FullName;

    // prepend current directory to name, if needed
    if (RawName[0] == '/') {
        FullName = RawName;
    } else {
        char cwd [4000];
        if (getcwd (cwd, sizeof(cwd)) == NULL)
            THROW_PBEXCEPTION ("getcwd failed: ");
        FullName = string(cwd) + "/" + RawName;
    }

    // tokenize by spliting on "/"
    vecstr Toks = Utils::SplitStr (FullName, "/");

    // discard all instances of "", ".", and "dir/.."
    for (auto itr = Toks.begin(); itr < Toks.end(); itr++) {
        if (*itr == "" || *itr == ".") {
            Toks.erase (itr);
            itr --;
        } else if (*itr == "..") {
            if (itr == Toks.begin()) {
                // handle .. at beginning
                Toks.erase (itr);
                itr --;
            } else {
                Toks.erase (itr-1, itr+1);
                itr -= 2;
            }
        }
    }

    // assemble the full path from the components
    string CanName = "/" + Utils::JoinStrs (Toks, "/");
    return CanName;
}

string Utils::JoinStrs (const vecstr &Parts, const string &Sep) {
    string Joined;
    bool first = 1;
    for (auto &Part : Parts) {
        if (!first)
            Joined += Sep;
        Joined += Part;
        first = false;
    }
    return Joined;
}

fstream Utils::OpenReadStream (const string &Name) {
    fstream Strm (Name.c_str(), fstream::in);
    if (Strm.fail())
        THROW_PBEXCEPTION_IO ("Can't open %s for read", Name.c_str());
    return Strm;
}

FILE* Utils::OpenReadBin (const string &Name) {
    FILE* F;
    if (!(F = fopen (Name.c_str(), "rb")))
        THROW_PBEXCEPTION_IO ("Can't open %s for read", Name.c_str());
    return F;
}

fstream Utils::OpenWriteStream (const string &Name) {
    fstream Strm (Name.c_str(), fstream::out);
    if (Strm.fail())
        THROW_PBEXCEPTION_IO ("Can't open %s for write", Name.c_str());
    return Strm;
}

FILE * Utils::OpenWriteBin (const string &Name) {
    FILE* F;
    if (!(F = fopen (Name.c_str(), "wb")))
        THROW_PBEXCEPTION_IO ("Can't open %s for write", Name.c_str());
    return F;
}

string Utils::ReadLine (fstream &Stream, bool DieOnEOF) {
    string Line;
    getline (Stream, Line);
    if (DieOnEOF && Stream.eof())
        THROW_PBEXCEPTION_IO ("Unexpected end of file");
    if (Stream.fail())
        THROW_PBEXCEPTION_IO ("ReadLine failed");
    return Line;
}

int Utils::ReadBinary (FILE *F, char *Buf, int BufSize) {
    int BytesRead;
    if ((BytesRead = fread (Buf, 1, BufSize, F)) < 0)
        THROW_PBEXCEPTION_IO ("Read failed");
    return BytesRead;
}

int Utils::ReadBinary (FILE *F, string &Str, int MaxSize) {
    Str.resize(MaxSize);
    int Size = ReadBinary (F, Str.data(), MaxSize);
    Str.resize (Size);
    return Size;
}

void Utils::WriteBinary (FILE *F, const char *Buf, unsigned BufSize) {
    if (fwrite (Buf, 1, BufSize, F) != BufSize)
        THROW_PBEXCEPTION_IO ("Write failed");
}

void Utils::WriteBinary (FILE *F, const string &Str) {
    WriteBinary (F, Str.c_str(), Str.size());
}

void Utils::CreateDir (const string Dir, bool CreateSubs) {
    error_code ec;
    if (CreateSubs) {
        if (fs::create_directories (Dir, ec))
            return;
    } else {
        if (fs::create_directory (Dir, ec))
            return;
    }
    if (!ec)
        return;
    THROW_PBEXCEPTION_IO ("Utils::CreateDir Failed to create %s: %s", Dir.c_str(), ec.message().c_str());
}

void Utils::SetModTime (const string &Name, uint64_t Time) {
    // set modification time
    struct timespec TV[2];
    uint64_t Ratio = 1000000000;
    TV[0].tv_sec  = Time / Ratio;
    TV[0].tv_nsec = Time % Ratio;
    TV[1] = TV[0];
    if (utimensat (AT_FDCWD, Name.c_str(), TV, AT_SYMLINK_NOFOLLOW))
        THROW_PBEXCEPTION_IO ("Can't set mod time of %s", Name.c_str()); 
}

void Utils::MakeHardLink (const string &ExistingFile, const string &NewName) {
    if (link (ExistingFile.c_str(), NewName.c_str()) != 0)
        THROW_PBEXCEPTION_IO ("Can't create hard link (%s) to target (%s)", NewName.c_str(), ExistingFile.c_str());
}

void Utils::CloneBlockDir (const string &SrcDir, const string &DstDir, BlockList &DstBlocks) {
    CreateDir (DstDir);

    vecstr SubDirs, SubFiles;
    SlurpDir (SrcDir, SubDirs, SubFiles);

    // hardlink all the files
    // add to allocated blocks
    for (auto &File : SubFiles) {
        MakeHardLink (SrcDir + "/" + File, DstDir + "/" + File);
        DstBlocks.MarkAllocated (strtoull(File.c_str(), NULL, 10));
    }

    // recurse into subdirs
    for (auto &Dir : SubDirs)
        CloneBlockDir (SrcDir + "/" + Dir, DstDir + "/" + Dir, DstBlocks);
}

void Utils::SlurpDir (const string &Dir, vecstr &SubDirs, vecstr &SubFiles) {
    for (const auto &entry : filesystem::directory_iterator(Dir)) {
        fs::path SubPath = entry.path();
        string   SubName = SubPath.filename().string();
        if (is_directory (SubPath))
            SubDirs.push_back(SubName);
        else if (is_regular_file (SubPath))
            SubFiles.push_back(SubName);
    }
}

void Utils::Touch (const string &Name) {
    fstream Strm (Name.c_str(), fstream::out | fstream::app);
    if (Strm.fail())
        THROW_PBEXCEPTION_IO ("Can't open %s for touch", Name.c_str());
    Strm.close();
}
