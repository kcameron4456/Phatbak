#include "ThreadPool.h"
#include "Archive.h"
#include "Comp.h"

// global thread pool structure
ThreadPool_t ThreadPool;

// when set, all threads will quit
volatile int StopThreads = 0;

// thread top level function
void BackGroundWorker (JobCtrl *Job) {
    try {
        // process jobs until top level says we're done
        while (1) {
            // wait for a new job
            DBG ("Job #%d, Wait for busy\n", Job->Idx);
            Job->Busy.WaitBusy ();
            if (StopThreads) {
                DBG ("Job #%d, Type=%d Abort\n", Job->Idx, Job->JobType);
                Job->Busy.PostIdle();
                break;
            }

            // process the job
            DBG ("Job #%d, Type=%d Starting\n", Job->Idx, Job->JobType);
            switch (Job->JobType) {
                case JobCtrl::CreateFile :
                    Job->CreateFileInfo.AF->CreateJob(
                        Job->CreateFileInfo.Keep
                        );
                    break;
                case JobCtrl::ExtractFile :
                    Job->ExtractFileInfo.Arch->DoExtractJob(
                          Job->ExtractFileInfo.ListLine
                         ,Job->ExtractFileInfo.LineNo
                        );
                    break;
                case JobCtrl::CompressChunk :
                    Job->CompressChunkInfo.AF->HashAndCompressJob (
                        Job->CompressChunkInfo.ChunkData
                       ,Job->CompressChunkInfo.RefChunkInfo
                       ,Job->CompressChunkInfo.RefBlockList
                       ,Job->CompressChunkInfo.HACR
                       );
                    Job->CompressChunkInfo.ChunkData.resize (0); // release potentially large buffer
                    break;
                case JobCtrl::ExtractChunk :
                    ExtractChunkJob (
                        Job->ExtractChunkInfo.Chunk
                       ,Job->ExtractChunkInfo.ChunkBlocks
                       ,Job->ExtractChunkInfo.BlockIdx
                       ,Job->ExtractChunkInfo.F
                       ,Job->ExtractChunkInfo.Lock
                       ,Job->ExtractChunkInfo.PrevLock
                    );
                    break;
                default:
                    THROW_PBEXCEPTION ("Unrecognized job type: %d", Job->JobType);
            }
            DBG ("Job #%d, Type=%d Finished\n", Job->Idx, Job->JobType);

            // release the thread to the pool
            Job->Busy.PostIdle();
            ThreadPool.ReleaseThread (Job);
        }
    }

    catch (PB_Exception &E) {
        E.Handle();
    }
}

// add n threads to the pool
void ThreadPool_t::AddThreads (int N) {
    // get the mutex
    unique_lock<mutex> lock(Mtx);

    // create the threads
    for (int i = N; i > 0; i--) {
        JobCtrl *Job = new JobCtrl (i);
        All  .push_back(Job);
        Avail.push_back(Job);
    }

    // tell any waiting threads
    CV.notify_all();
}

void ThreadPool_t::WaitIdle () {
    unique_lock<mutex> lock(Mtx);
    CV.wait (lock, [this]{return All.size() == Avail.size();});
}

// call this after all work for the threads is complete
void ThreadPool_t::JoinAll () {
    DBG ("ThreadPool_t::JoinAll()\n");
    StopThreads = 1;

    // tell any waiting threads
    for (auto &Job : All) {
        Job->Notify();
        Job->Join();
    }
}

// allocate a thread to be used for a job
JobCtrl * ThreadPool_t::AllocThread (bool Wait) {
    // get the mutex
    unique_lock<mutex> lock(Mtx);

    // wait for available thread
    CV.wait (lock, [this, Wait]{return !Avail.empty() || !Wait;});
    if (Avail.empty())
        return NULL;

    // grab a thread
    JobCtrl *rval = Avail.back();
    Avail.pop_back();

    // return the thread pointer
    return rval;
}

void ThreadPool_t::ReleaseThread (JobCtrl *Job) {
    // get the mutex
    unique_lock<mutex> lock(Mtx);

    // add the job to the queue
    Avail.push_back (Job);

    CV.notify_all();
}
