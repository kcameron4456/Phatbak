#ifndef LOGGING_H
#define LOGGING_H

#include "Opts.h"
#include "Utils.h"

#include <stdio.h>
#include <string>
#include <stdarg.h>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

#define BOOST_STACKTRACE_USE_ADDR2LINE
#define BOOST_STACKTRACE_DYN_LINK
#define BOOST_STACKTRACE_LINK
#define BOOST_ALL_DYN_LINK
#include <boost/stacktrace.hpp>

using namespace std;

extern recursive_mutex Logging_Mutex;

#ifdef DBGMSG
#define DBG(...) {if (O.DebugPrint) { \
                  unsigned long nowns = chrono::duration_cast<chrono::nanoseconds> \
                                        (chrono::system_clock::now().time_since_epoch()).count(); \
                  Logging_Mutex.lock(); \
                  fprintf (stderr, "%llu: ", nowns % 100000000000000ULL); \
                  fprintf (stderr, __VA_ARGS__); \
                  fflush  (stderr); \
                  Logging_Mutex.unlock(); \
                 }}
#define DBGALLOC(...) DBG(__VA_ARGS__) // TBD: add option to turn these off
#define DBGCTOR       DBGALLOC("%s CTOR:%p\n", __PRETTY_FUNCTION__, this)
#define DBGDTOR       DBGALLOC("%s DTOR:%p\n", __PRETTY_FUNCTION__, this)
#else
#define DBG(...)
#define DBGALLOC(...)
#define DBGCTOR
#define DBGDTOR
#endif

#define WARN(...) {                            \
    Logging_Mutex.lock();                      \
    fprintf (stderr, "Warning: " __VA_ARGS__); \
    Logging_Mutex.unlock();                    \
}

#define ERROR(...) {                         \
    Logging_Mutex.lock();                    \
    fprintf (stderr, "Error: " __VA_ARGS__); \
    Logging_Mutex.unlock();                  \
    exit (1);                                \
}

// exception handling
#define PB_VARGS(a) va_list args; va_start(args,a)
class PB_Exception {
    public:
    string Message;
    string MsgType;
    void PrintMessage () {
        fprintf (stderr, "%s\n", Message.c_str());
    }
    void MakeMessage (int SrcLine, const char *SrcFile, const string &fmt, va_list args) {
        char LBuf [100];
        snprintf (LBuf, 99, "%d", SrcLine);
        string NewFmt = (string)SrcFile + ":" + LBuf + ": " + MsgType + fmt;

        char cmsg [4000];
        vsnprintf (cmsg, 3999, NewFmt.c_str(), args);
        Message  = Backtrace();
        Message += cmsg;
    }
    void MakeMessage (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
    string Backtrace () {
        stringstream res;
        #if (0)
            // this needs work
            namespace bs = boost::stacktrace;
            bs::stacktrace st;
            int Cnt = 0;
            for (auto frame: st)
                res
                    << setw(2) << Cnt++ << "# " << setw(0)
                    << frame.name()
                    << ":" << frame.source_file()
                    << ":" << frame.source_line()
                    << endl;
        #else
            res << boost::stacktrace::stacktrace() << endl;
        #endif

        // get rid of linese with PB_Exception
        string Traces = res.str();
        vecstr Lines = Utils::SplitStr (Traces, "\n");
        vecstr ResLines;
        for (auto &Line : Lines)
            if (Line.find ("PB_Exception") == string::npos)
                ResLines.push_back(Line);
        string Result = Utils::JoinStrs (ResLines, "\n");

        return Result;
    }
    virtual void Print () {
        PrintMessage ();
    }
    virtual void Handle () {
        Print();
        exit (1);
    }
    PB_Exception () {
        Message = "PB_Exception - undefined exception type";
    }
    PB_Exception (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        MsgType = "PhatBak Error: ";
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};
class PB_ExceptionFMT : public PB_Exception {
    public:
    PB_ExceptionFMT (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        MsgType = "PhatBak Format Error: ";
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};
class PB_ExceptionIO : public PB_Exception {
    int ErrNo;
    public:
    void Print () {
        errno = ErrNo;
        perror (Message.c_str());
    }
    void Handle () {
        Print ();
        exit (ErrNo);
    }
    PB_ExceptionIO (int SrcLine, const char *SrcFile, const string &fmt, ...) {
        MsgType = "PhatBak IO Error: ";
        ErrNo = errno;
        PB_VARGS(fmt);
        MakeMessage (SrcLine, SrcFile, fmt, args);
        va_end(args);
    }
};

// macros to include file name and line number in exception
#define THROW_PBEXCEPTION_GEN(TYPE, ...) throw PB_Exception##TYPE (__LINE__, __FILE__, __VA_ARGS__)
#define THROW_PBEXCEPTION_IO(...)        THROW_PBEXCEPTION_GEN (IO , __VA_ARGS__)
#define THROW_PBEXCEPTION_FMT(...)       THROW_PBEXCEPTION_GEN (FMT, __VA_ARGS__)
#define THROW_PBEXCEPTION(...)           THROW_PBEXCEPTION_GEN (   , __VA_ARGS__)

#endif // LOGGING_H
