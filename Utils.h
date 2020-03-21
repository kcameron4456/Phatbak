#ifndef UTILS_H
#define UTILS_H

#include "Types.h"
#include "BlockList.h"

#include <vector>
#include <string>
#include <fstream>
#include <stdio.h>
#include <sys/stat.h>
using namespace std;

namespace Utils {
    // split string into in parts delimited by pattern
    vecstr SplitStr (string Src, const string &Pat);

    // remove all white space from beginning and end of string
    void   TrimStr (string *Str);
    string TrimStr (string  Str);

    // create full path name to file without extraneous relatime paths 
    string CanonizeFileName (const string &RawName);

    // join multiple strings into one with optional separater
    string JoinStrs (const vecstr &Parts, const string &Sep = "");

    // open file stream for input
    fstream OpenReadStream (const string &Name);

    // open file stream for binary input
    FILE * OpenReadBin (const string &Name);

    // open file stream for output
    fstream OpenWriteStream (const string &Name);

    // open file stream for binary output
    FILE * OpenWriteBin (const string &Name);

    // read a line from a file stream, optionally die on eof
    string ReadLine (fstream &Stream, bool DieOnEOF = 1);

    // read unformatted data into byte buffer
    int ReadBinary (FILE *F, char *Buf, int BufSize);

    // read unformatted data into string
    int ReadBinary (FILE *F, string &Str, int MaxSize);

    // write unformatted data to file
    void WriteBinary (FILE *F, const char *Buf, unsigned BufSize);
    void WriteBinary (FILE *F, const string &Str);

    // create a directory - optionally create needed subdirs
    void CreateDir (const string Dir, bool CreateSubs = false);

    // hard link one file to another
    void MakeHardLink (const string &ExistingFile, const string &NewName);

    // create a hardlinked copy of an existing block directory (FInfo or Chunks)
    void CloneBlockDir (const string &DirName, const string &DstName, BlockList &BL);

    // return all the subdirectories and files within a directory
    void SlurpDir (const string &Dir, vecstr &SubDirs, vecstr &SubFiles);

    // touch a file (create or update modification time)
    void Touch (const string &Name);

    // link a file to a new name
    void Link (const string &Name, const string &Target);

    // extract standard stat type from archive file stats header
    struct stat ParseStatsHeader (const string &Hdr);

    // create archive file stats header from standard stat type
    string CreateStatsHeader (const struct stat &Stats);

    // convert u64 ns time to stats time structure
    timespec NsToTimeSpec (u64 ns);

    // convert stats time_t to ns time
    u64 TimeSpec_ToNs (const timespec &T);

    // return true if two timespecs are equal
    bool TimeSpecsEqual (const timespec &T1, const timespec &T2);

    // create a unix domain socket
    void CreateSocket (const string &Name);

    // set mode bits (really just permissions) of a file
    void SetMode (const string &Name, mode_t Mode);

    // set owner and group of a file
    void SetOwn  (const string &Name, u32 Uid, u32 Gid, bool IsSLink = false);

    // set modification time on a file
    void SetModTime (const string &Name, timespec Time);
}

#endif // UTILS_H
