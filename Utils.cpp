#include "Utils.h"
#include "Logging.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <acl/libacl.h>
#include <sys/acl.h>
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
    assert (F);
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
    assert (F);
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
    assert (F);
    int BytesRead;
    if ((BytesRead = fread (Buf, 1, BufSize, F)) < 0)
        THROW_PBEXCEPTION_IO ("Read failed");
    return BytesRead;
}

int Utils::ReadBinary (FILE *F, string &Str, int MaxSize) {
    assert (F);
    Str.resize(MaxSize);
    int Size = ReadBinary (F, Str.data(), MaxSize);
    Str.resize (Size);
    return Size;
}

void Utils::WriteBinary (FILE *F, const char *Buf, unsigned BufSize) {
    assert (F);
    if (fwrite (Buf, 1, BufSize, F) != BufSize)
        THROW_PBEXCEPTION_IO ("Write failed");
}

void Utils::WriteBinary (FILE *F, const string &Str) {
    assert (F);
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
    if (ec)
        THROW_PBEXCEPTION ("Utils::CreateDir Failed to create %s: %s", Dir.c_str(), ec.message().c_str());
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

void Utils::Link (const string &Name, const string &Target) {
    error_code ec;
    fs::create_hard_link (Target, Name, ec);
    if (ec)
        THROW_PBEXCEPTION_IO ("Error creating link:%s to target:%s :%s", Name.c_str(), Target.c_str(), ec.message().c_str());
}

// extract standard stat type from archive file stats header
struct stat Utils::ParseStatsHeader (const string &Hdr) {
    struct stat Stats;
    vecstr Fields = SplitStr (Hdr, " ");
    for (auto Field : Fields) {
        vecstr Two = SplitStr (Field, ":");
        if (Two.size() != 2)
            THROW_PBEXCEPTION_FMT ("Illegal header field : %s", Field.c_str());
        string &Name = Two[0];
        string &Val  = Two[1];
             if (Name == "mode" ) Stats.st_mode =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "uid"  ) Stats.st_uid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "gid"  ) Stats.st_gid  =               strtoull (Val.c_str(), NULL, 16);
        else if (Name == "size" ) Stats.st_size =               strtoull (Val.c_str(), NULL, 10);
        else if (Name == "mtime") Stats.st_mtim = NsToTimeSpec (strtoull (Val.c_str(), NULL, 16));
    }
    return Stats;
}

// create archive file stats header from standard stat type
string Utils::CreateStatsHeader (const struct stat &Stats) {
    stringstream res;
    res << "mode:"  << hex <<                Stats.st_mode  << " ";
    res << "uid:"   << hex <<                Stats.st_uid   << " ";
    res << "gid:"   << hex <<                Stats.st_gid   << " ";
    res << "size:"  << dec <<                Stats.st_size  << " "; // keep size in decimal to make it easier to read and debug
    res << "mtime:" << hex << TimeSpec_ToNs (Stats.st_mtim) << " ";

    return res.str();
}

// convert u64 nanosecond time to stats time structure
timespec Utils::NsToTimeSpec (u64 ns) {
    timespec T;
    static const u64 Ratio = 1000000000;
    T.tv_sec  = ns / Ratio;
    T.tv_nsec = ns % Ratio;
    return T;
}

// convert stats time_t to ns time
u64 Utils::TimeSpec_ToNs (const timespec &T) {
    return (u64) T.tv_sec * 1000000000 + (u64) T.tv_nsec;
}

// return true if two timespecs are equal
bool Utils::TimeSpecsEqual (const timespec &T1, const timespec &T2) {
    return T1.tv_sec == T2.tv_sec && T1.tv_nsec == T2.tv_nsec;
}

// create a socket
void Utils::CreateSocket (const string &Name) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        THROW_PBEXCEPTION_IO ("Can't create socket:", Name.c_str());

    struct sockaddr_un server;
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, Name.c_str(), sizeof(server.sun_path)-1);
    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
        THROW_PBEXCEPTION_IO ("Can't bind socket: %s", Name.c_str());
    }
    close (sock);
}

void Utils::SetMode (const string &Name, mode_t Mode) {
    if (chmod (Name.c_str(), Mode))
        fprintf (stderr, "Can't set dir/file directory (%s) mode to %o: %s\n", Name.c_str(), Mode, strerror(errno));
}

void Utils::SetOwn (const string &Name, u32 Uid, u32 Gid, bool IsSLink) {
    int res;
    if (IsSLink)
        res = lchown (Name.c_str(), Uid, Gid);
    else
        res =  chown (Name.c_str(), Uid, Gid);
    if (res)
        fprintf (stderr, "Can't set dir/file (%s) owner/group to (%d/%d): %s\n",
                              Name.c_str(), Uid, Gid, strerror(errno));
}

void Utils::SetModTime (const string &Name, timespec Time) {
    // set modification time
    struct timespec TV[2] = {Time, Time};
    if (utimensat (AT_FDCWD, Name.c_str(), TV, AT_SYMLINK_NOFOLLOW))
        THROW_PBEXCEPTION_IO ("Can't set mod time of %s", Name.c_str()); 
}

string Utils::GetFileAcl (const string &Name, u16 Perm, u32 Type) {
    // get file acl struture
    auto Acl = acl_get_file(Name.c_str(), Type);
    if (Acl == 0)
        return ""; // no acl of this type
    if (Acl < 0)
        THROW_PBEXCEPTION_IO ("Can't get acl for: %s", Name.c_str());

    // filter out base acl entries
    acl_t NewACL = acl_init(0);
    acl_entry_t Entry;
    for (int EntStat = acl_get_entry (Acl, ACL_FIRST_ENTRY, &Entry);
             EntStat == 1;
             EntStat = acl_get_entry (Acl, ACL_NEXT_ENTRY , &Entry)) {

        // get entry type
        acl_tag_t tag;
        acl_get_tag_type(Entry, &tag);

        // compare base acl to normal permissions mask returned my lstat
        if (Type == ACL_TYPE_ACCESS) {
            // get permissions for this entry
            acl_permset_t PermSet;
            acl_get_permset (Entry, &PermSet);

            // if base permission is the same as perm from lstat call, ignore this entry
            if (tag == ACL_USER_OBJ  && (*(u8 *)PermSet & 7) == ((Perm >> 6) & 7)) continue;
            if (tag == ACL_GROUP_OBJ && (*(u8 *)PermSet & 7) == ((Perm >> 3) & 7)) continue;
            if (tag == ACL_OTHER     && (*(u8 *)PermSet & 7) == ((Perm >> 0) & 7)) continue;
        }

        acl_entry_t NewEntry;
        if (acl_create_entry (&NewACL, &NewEntry) < 0)
            THROW_PBEXCEPTION_IO ("Can't create acl entry\n");
        if (acl_copy_entry (NewEntry, Entry) < 0)
            THROW_PBEXCEPTION_IO ("Can't copy acl entry\n");
    }

    // if no acls remain, we're done
    if (acl_entries(NewACL) <= 0) {
        acl_free (Acl);
        acl_free (NewACL);
        return "";
    }

    // convert to short form text
    char *AclText = acl_to_any_text (NewACL, NULL, ',', TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
    string AclStr (AclText);

    // release resources
    acl_free (AclText);
    acl_free (Acl);
    acl_free (NewACL);

    string Result;
    Result += (Type == ACL_TYPE_DEFAULT ? 'D' : 'A');
    Result += string ("|") + AclStr;

    return Result;
}

string Utils::GetFileAcls (const string &Name, u16 Perm) {
    vecstr ACLs;
    for (int Type : {ACL_TYPE_ACCESS, ACL_TYPE_DEFAULT}) {
        string ACLStr = GetFileAcl (Name, Perm, Type);
        if (ACLStr.size())
            ACLs.push_back(ACLStr);
    }

    if (ACLs.size())
        return JoinStrs (ACLs, ";");

    return "";
}

static bool             LookupInitted = 0;
static mutex            LookupMtx;
static map <u8, string> Lookup;
void Utils::SetFileAcls (const string &Name, u16 Perm, const string &Acls) {
    if (!Acls.size())
        return;

    // build lookup table for perm field to string
    LookupMtx.lock();
    if (!LookupInitted) {
        for (u8 i = 0; i <= 7; i++) {
            string Val = string (1, i&4 ? 'r' : '-') +
                         string (1, i&2 ? 'w' : '-') +
                         string (1, i&1 ? 'x' : '-');
            Lookup [i] = Val;
        }
        LookupInitted = 1;
    }
    LookupMtx.unlock();

    // break acl spec into access and default portions
    for (auto Acl : SplitStr (Acls, ";")) {
        // split off type specifier
        vecstr Parts = SplitStr (Acl, "|");
        if (Parts.size() != 2)
            THROW_PBEXCEPTION_FMT ("Illegal ACL format: %s\n", Acl.c_str());
        u32 Type;
        if (Parts[0] == "A")
            Type = ACL_TYPE_ACCESS;
        else if (Parts[0] == "D")
            Type = ACL_TYPE_DEFAULT;
        else
            THROW_PBEXCEPTION_FMT ("Illegal ACL format: %s\n", Acl.c_str());
        string AclShort = Parts[1];

        // add base permissions, if needed
        if (Type == ACL_TYPE_ACCESS) {
            if (AclShort.find ("u::")==AclShort.npos) AclShort += ",u::" + Lookup [(Perm >> 6) & 7];
            if (AclShort.find ("g::")==AclShort.npos) AclShort += ",g::" + Lookup [(Perm >> 3) & 7];
            if (AclShort.find ("o::")==AclShort.npos) AclShort += ",o::" + Lookup [(Perm >> 0) & 7];
        }

//fprintf (stderr, "Name=%s Perm =%03o\n", Name.c_str(), Perm & 0777);
//fprintf (stderr, "%c Type =%ld Parts[1]=%s\n", Parts[0][0], (long) Type, Parts[1].c_str());
//fprintf (stderr, "%c Type =%ld AclShort=%s\n", Parts[0][0], (long) Type, AclShort.c_str());
        // set the file acl
        acl_t acl = acl_from_text (AclShort.c_str());
        if (!acl)
            THROW_PBEXCEPTION_FMT ("Can't parse acl text: %s: %s", AclShort.c_str(), strerror(errno));
        if (acl_set_file (Name.c_str(), Type, acl))
            fprintf (stderr, "Can't set acl for file: %s: %s\n", Name.c_str(), strerror(errno));
        acl_free (acl);
    }
}
