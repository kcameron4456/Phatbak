#include "Opts.h"
#include "Utils.h"

#include <grp.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
using namespace std;

// global options declaration
Opts O;

// default version - overriden in Makefile from git release tag
#ifndef VERSION_MAJOR
    #define VERSION_MAJOR 0
#endif
#ifndef VERSION_MINOR
    #define VERSION_MINOR 1
#endif

#define MAXARGVALLEN 1000   // maximum length of individual command line arguments

static void PrintHelp(int rval=0) {
    printf (R"(
Usage: PhatBak [Operation Tags] [Flags] Repo::Archive [file/dir list]
    Archives data to backup medium or device or Extract backed up data
    Every effort is made to enable restoring:
       File/Dir contents
       File/Dir modification times
       Hard links
       Symbolic links
    If ArchiveFile is '-' or not given, use stdin or stdout

Arguments:
    "Operation Args" Tags optional gnu-tar like tags in the set [cxtDvf].  Tags in [cxtDv] behave exactly
    like the "-" versions of the flags shown below.  The "f" tag is similar except that the archive
    file name can appear anywhere later on the command line.

    Main Operation Flags (exactly one of these must be specified on the command line):
    -c   Create       : Create an archive from the list of files and directories
    -x   Extract      : Extract archived files into the current working directory
    -t   Test         : Read archive file and test for conformance to PhatBak format

    Optional Flags
    -f   FileName     : Archive file name.  If "-" or not given, use stdin (Extract and Test) or stdout (Create)

    -v   Show         : Show file names on stderr while writing or reading the archive file 
    -D   Dump         : Show PhatBak file structure on stderr during Test

    -T   Thread Count : Number of threads to allocate (default 200)
                        Create only at this time
                        Note these are generally lightweight threads which allow the create code to search
                        well ahead and prime output buffers while a thread is busy copying a file

    -Bs  Buffer Size  : Size of file copy buffers (default 300000)
    -Bc  Buffer Count : Number of copy buffers (and threads) for each file (default 12)
                        Doubled when compression is used

    -iZ  Compress     : Compress file blocks during Create using zstdlib
    -iZl Level        : Compression level (effort)

    --version         : Output version information and exit

    -h -help --help   : Display this help and exit
)");

    exit (rval);
}

static void ArgError (const char *arg, const char *nxtarg = NULL) {
    const char *a = nxtarg ? nxtarg : "";
    fprintf (stderr, "ERROR: Illegal Argument: %s %s\n", arg, a);
    PrintHelp(1);
}

void PrintOpHelp () {
    fprintf (stderr, "ERROR: Exactly one flag of type {c -c x -x t -t} must be given\n");
    PrintHelp(1);
}

static void PrintVersion() {
    fprintf (stderr, "Version:  %d.%d\n", VERSION_MAJOR, VERSION_MINOR);
    exit (0);
}

// maybe use this for sizes someday
#if 0
static int ParseSize (const char *SizeStr) {
    const char *Save = SizeStr;
    int Num = 0;

    // handle number
    while (*SizeStr) {
        if (*SizeStr >= '0' && *SizeStr <= '9') {
            Num *= 10;
            Num += *SizeStr - '0';
        } else {
            break;
        }
        SizeStr++;
    }

    // handle units
    if (!strcmp (SizeStr, "KiB"))
        Num *= 1024;
    else if (!strcmp (SizeStr, "MiB"))
        Num *= 1024 * 1024;
    else if (!strcmp (SizeStr, "GiB"))
        Num *= 1024 * 1024 * 1024;
    else if (!strcmp (SizeStr, "KB"))
        Num *= 1000 ;
    else if (!strcmp (SizeStr, "MB"))
        Num *= 1000 * 1000;
    else if (!strcmp (SizeStr, "GB"))
        Num *= 1000 * 1000 * 1000;
    else if (*SizeStr)
        ArgError (Save);

    if (!Num)
        ArgError (Save);

    return Num;
}
#endif

// macro to skip to the next argument
#define NXTARG { \
    argidx++; \
    if (argidx >= argc) \
        ArgError (arg); \
    arg = argv[argidx]; \
    if (strlen (arg) >= (MAXARGVALLEN-2)) \
        ArgError (arg); \
}

// parse command line options
#define TESTOP {if (Operation != DoUndef) PrintOpHelp();}
void Opts::ParseCmdLine (const int argc, const char *argv[]) {
    // apply defaults
    VersionMajor    = VERSION_MAJOR;
    VersionMinor    = VERSION_MINOR;
    Operation       = DoUndef;
    ShowFiles       = 0;
    NumThreads      = 200;
    IntCompType     = 0; // TBD: TTCMPT_NONE;
    IntComprLvl     = 2;
    BlockNumDigits  = 2;
    ChunkSize       = 1 << 18;
    HashType        = HashType_MD5;
    ExtractTarget   = "PhatBakExtract";
    BlockNumModulus = 1;
    for (int i = 0; i < BlockNumDigits; i++)
        BlockNumModulus *= 10;

    // save command line
    int argidx;
    for (argidx=0; argidx < argc; argidx++) {
        if (argidx)
            CmdLine += " ";
        CmdLine += argv[argidx];
    }

    // step through all "-" and "--" args
    for (argidx = 1; argidx < argc; argidx++) {
        const char *arg = argv[argidx];

        // test non-minus arg
        if (arg[0] != '-') {
            // it's the end of minus args
            break;
        }

        // begin generic argument parsing
        #define PARSE_MinusVal(Name, ValFmt, ValPtr, Action) { \
            if (!strcmp (arg, Name)) { \
                NXTARG \
                if (sscanf (arg, ValFmt, ValPtr) != 1) \
                    ArgError (arg, arg); \
                Action \
                continue; \
            } \
        }
        #define PARSE_MinusStr(Name, ValNam, Action) { \
            if (!strcmp (arg, Name)) { \
                NXTARG \
                ValNam = arg; \
                Action \
                continue; \
            } \
        }
        #define PARSE_MinusFlg(Name, Test, ValNam, Val, Action) { \
            if (!strcmp (arg, Name)) { \
                Test \
                ValNam = Val; \
                Action \
                continue; \
            } \
        }

        //int TmpInt = 0;
        PARSE_MinusFlg ("-c"                 , TESTOP, Operation  , DoCreate , )
        PARSE_MinusFlg ("-x"                 , TESTOP, Operation  , DoExtract, )
        PARSE_MinusFlg ("-t"                 , TESTOP, Operation  , DoTest   , )
        PARSE_MinusFlg ("-v"                ,, ShowFiles  , 1,)
        PARSE_MinusFlg ("-D"                ,, ShowFiles=ArchDiag, 1, )
        PARSE_MinusVal ("-T"                ,"%d", &NumThreads,)
        // TBD: PARSE_MinusFlg ("-iZ"            ,,  TmpInt   , 1, IntCompType=TTCMPT_ZSTD;)
        PARSE_MinusVal ("-iZl","%d"         , &IntComprLvl,)
        PARSE_MinusStr ("--HashType"        , arg, HashType = HashNameToEnum(arg);)
        PARSE_MinusVal ("--ChunkSize"       ,"%d", &ChunkSize,)
        PARSE_MinusVal ("--BlockNumDigits"  ,"%d", &BlockNumDigits,)
        PARSE_MinusVal ("--BlockNumModulus" ,"%d", &BlockNumModulus,)
        PARSE_MinusFlg ("-h"                ,, arg     , arg, PrintHelp();)
        PARSE_MinusFlg ("-help"             ,, arg     , arg, PrintHelp();)
        PARSE_MinusFlg ("--help"            ,, arg     , arg, PrintHelp();)
        PARSE_MinusFlg ("--version"         ,, arg     , arg, PrintVersion();)

        ArgError(arg);
    }

    // basic operation must be set
    if (Operation == DoUndef)
        PrintHelp(1);

    // first remaining arg is the repo/archive name
    if (argidx >= argc) {
        printf ("No Repo::Archive argument given\n");
        PrintHelp(1);
    }
    string RepoArchName = argv[argidx++];
    vector <string> Parts = Utils::SplitStr (RepoArchName, "::");
    if (Parts.size() != 2) {
        printf ("Unrecognized Repo::Archive format: %s\n", RepoArchName.c_str());
        PrintHelp(1);
    }
    RepoDirName = Parts[0];
    ArchDirName = Parts[1];

    // remaining args are file/dir names for create or extract
    // default to cwd
    if (argidx >= argc)
        FileArgs.push_back (".");
    for (; argidx < argc; argidx++)
        FileArgs.push_back (argv[argidx]);

    //Print();
}

Opts::Opts () {
    StartTime = time(NULL);
}

void Opts::Print (fstream &F) {
    F << OptsString ();
}

void Opts::Print (ostream &F) {
    F << OptsString ();
}

string Opts::OptsString () {
    stringstream F;
    F << "Options:\n";
    F << "   CmdLine         = " << CmdLine                         << endl;
    F << "   Operation       = " << OpText()                        << endl;
    F << "   RepoDirName     = " << RepoDirName                     << endl;
    F << "   ArchDirName     = " << ArchDirName                     << endl;
    F << "   ShowFiles       = " << ShowFiles                       << endl;
    F << "   FileArgs        = " << Utils::JoinStrs (FileArgs, " ") << endl;
    F << "   BlockNumDigits  = " << BlockNumDigits                  << endl;
    F << "   BlockNumModulus = " << BlockNumModulus                 << endl;
    F << "   ChunkSize       = " << ChunkSize                       << endl;
    F << "   HashType        = " << HashNames[HashType]             << endl;
    return F.str();
}
