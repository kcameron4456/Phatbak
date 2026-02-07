#ifndef OPTS_H
#define OPTS_H

#include "Types.h"
#include "Hash.h"
#include "Comp.h"

#include <string>
#include <vector>
#include <list>
#include <stdio.h>
#include <ctime>
#include <fstream>
#include <iostream>
using namespace std;

// Structure to hold option values
class Opts {
    public:
    int       VersionMajor;     // tartar program version
    int       VersionMinor;     // tartar program version
    string    CmdLine;          // Command line used to invoke tartar
    u64       StartTime;        // Time this program started (epoch ns)
    string    StartTimeTxt;     // Time this program started (text version)
    vecstr    FileArgs;         // List of file names to be archived or extracted
    string    CWD;              // Current working dir (maybe needed to give context to FileArgs)
    string    RepoDirName;      // Repository dir for archives
    string    ArchDirName;      // Archive directory withing the repo
    int       BlockNumModulus;  // Amount by which divide block indices to create block levels
    unsigned  ChunkSize;        // Max size of data blocks into which file data are stored
    eHashType HashType;         // hash algorithm
    eCompType CompType;         // type of per-file-block compression to use
    bool      ShowFiles;        // Show file names as they are archived or extracted
    bool      ArchDiag;         // Show diagnostic for archive file blocks in Test mode
    int       NumThreads;       // number of helper threads to launch
    int       CompLevel;        // compression effort
    string    ExtractTarget;    // directory into which to place files extracted from an Archive
    bool      Rebase;           // true to force a new base archive on create
    string    BaseArchive;      // user-specified base archive
    bool      DebugPrint;       // true output trace info for debug

    enum OpEnum { DoUndef = 0
                 ,DoInit
                 ,DoCreate
                 ,DoExtract
                 ,DoTest
                 ,DoCompare
                 ,DoList
                 ,DoShowLatest
                 ,DoVoid  // marks end of operations
                } Operation; // what to do

    Opts ();

    void ParseCmdLine (const int argc, const char *argv[]);

    string OpText (OpEnum Op = DoUndef) {
        if (Op == DoUndef)
            Op = Operation;
        return Op == DoUndef      ? "undef"   :
               Op == DoInit       ? "init"    :
               Op == DoCreate     ? "create"  :
               Op == DoExtract    ? "extract" :
               Op == DoTest       ? "test"    :
               Op == DoCompare    ? "compare" :
               Op == DoList       ? "list"    :
               Op == DoShowLatest ? "latest"  :
                                    "illegal" ;
    }

    void   Print      (fstream &F);
    void   Print      (ostream &F = cout);
    string OptsString ();
} ;

// global options pointer
extern Opts O;

#endif // OPTS_H
