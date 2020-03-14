#ifndef OPTS_H
#define OPTS_H

#include "Hash.h"

#include <string>
#include <vector>
#include <list>
#include <stdio.h>
#include <ctime>
#include <fstream>
#include <iostream>
using namespace std;

// Structure to hold option values
typedef vector<string> StrList_t;
class Opts {
    public:
    int       VersionMajor;     // tartar program version
    int       VersionMinor;     // tartar program version
    string    CmdLine;          // Command line used to invoke tartar
    time_t    StartTime;        // Time this program started
    StrList_t FileArgs;         // List of file names to be archived or extracted
    string    RepoDirName;      // Repository dir for archives
    string    ArchDirName;      // Archive directory withing the repo
    int       BlockNumDigits;   // Number of digits to assign to each level of a blocked directory
    int       BlockNumModulus;  // Amount by which divide block indices to create block levels
    eHashType HashType;         // hash algorythm
    int       ChunkSize;        // Max size of data blocks into which file data are stored
    bool      ShowFiles;        // Show file names as they are archived or extracted
    bool      ArchDiag;         // Show diagnostic for archive file blocks in Test mode
    int       NumThreads;       // number of helper threads to launch
    int       IntCompType;      // type of per-file-block compression to use (must be in TTCompType_e)
    int       IntComprLvl;      // compression effort
    string    ExtractTarget;    // directory into which to place files extracted from an Archive
    bool      DebugPrint;       // true output trace info for debug

    enum      {DoUndef  , DoCreate,
               DoExtract, DoTest  } Operation; // what to do

    Opts ();

    void ParseCmdLine (const int argc, const char *argv[]);

    const char *OpText () {
        return Operation == DoUndef   ? "DoUndef"   :
               Operation == DoCreate  ? "DoCreate"  :
               Operation == DoExtract ? "DoExtract" :
               Operation == DoTest    ? "DoTest"    :
                                        "Illegal"   ;
    }

    void   Print      (fstream &F);
    void   Print      (ostream &F = cout);
    string OptsString ();
} ;

// global options pointer
extern Opts O;

#endif // OPTS_H
