#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "Logging.h"
#include "Archive.h"
#include "BusyLock.h"

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

// class to contain info needed to start a job on a waiting thread
// one of these is owned by each worker
class JobCtrl;
extern void BackGroundWorker (JobCtrl *);
class JobCtrl {
    private:

    public:
    int          Idx;  // sequential count to assist debug
    thread      *Thr;  // pointer to thread control structure 
    BusyLock     Busy; // tells worker when there's work to be done

    // tells the thread what type of work to do
    enum {
        CreateFile = 1  , // create archive file
        ExtractFile     , // extract file from archive
        CompressChunk   , // compress one data chunk and write it to the archive
        ExtractChunk    , // extract chunk info into restored file
        ReverseAlloc    , // reverse allocate blocks from actual disk files
        CloneBlocks     , // clone blocks from another archive
    } JobType;

    struct {
        ArchFileCreate *AF;
        bool            Keep;
    } CreateFileInfo;
    struct {
        ArchiveRead *Arch;
        string       ListLine;
        u64          LineNo;
    } ExtractFileInfo;
    struct {
              ArchFileCreate        *AF;
              string                 ChunkData;
        const ChunkInfo             *BaseChunkInfo;
              BlockList             *BaseBlockList;
              HashAndCompressReturn *HACR;
    } CompressChunkInfo;
    struct {
        const ChunkInfo *Chunk;
        const BlockList *ChunkBlocks;
        i64              BlockIdx;
        FILE            *F;
        BusyLock        *Lock;
        BusyLock        *PrevLock;
    } ExtractChunkInfo;
    struct {
        BlockList *Blocks;
    } ReverseAllocInfo;
    struct {
              BlockList *TargetBlocks;
        const BlockList *SourceBlocks;
    } CloneBlocksInfo;

    JobCtrl (int idx) {
        Idx = idx;
        Thr = new thread (BackGroundWorker, this);
    }
    ~JobCtrl () {
        delete Thr;
    }
    void Join() {
        Thr->join();
    }
    void Notify () {
        Busy.Notify();
    }
    void Go () {
        Busy.PostBusy();
    }
};

// list of thread control structures
// each element of the list represents a thread
// which is available to start a new job
class ThreadPool_t {
    vector <JobCtrl *> All;   // list of all Worker threads
    vector <JobCtrl *> Avail; // set of Worker threads which are ready to perform work
    mutex              Mtx;   // mutex to control alloc/release
    condition_variable CV;    // condition variable for alloc/release

    // for debug with gdb
    static const int JobArraySize = 100;
    JobCtrl *JobArray [JobArraySize];

    public:

    ThreadPool_t () {
        DBGCTOR;
    }

    ~ThreadPool_t () {
        DBGDTOR;
        JoinAll();
        for (auto &Job : All)
            delete Job;
    }

    void     AddThreads    (int N);
    JobCtrl *AllocThread   (bool Wait = 1);
    void     ReleaseThread (JobCtrl *Job);
    void     WaitIdle      ();  // wait for all jobs to finish
    void     JoinAll       ();
};

// global job pool
extern ThreadPool_t ThreadPool;

#endif // THREADPOOL_H
