#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <fstream>
#include <stdio.h>
using namespace std;

namespace Utils {
    // split string into in parts delimited by pattern
    vector <string> SplitStr (string Src, const string &Pat);

    // remove all white space from beginning and end of string
    void   TrimStr (string *Str);
    string TrimStr (string  Str);

    // create full path name to file without extraneous relatime paths 
    string CanonizeFileName (const string &RawName);

    // join multiple strings into one with optional separater
    string JoinStrs (const vector <string> &Parts, const string &Sep = "");

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

    // read unformatted data into byte buffer
    void WriteBinary (FILE *F, const char *Buf, unsigned BufSize);
    void WriteBinary (FILE *F, const string &Str);

    // create a directory - optionally create needed subdirs
    void CreateDir (const string Dir, bool CreateSubs = false);
}

#endif // UTILS_H
