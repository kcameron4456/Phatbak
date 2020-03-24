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
                DBG ("Job #%d Abort\n", Job->Idx);
                Job->Busy.PostIdle();
                break;
            }

            // process the job
            DBG ("Job #%d, Starting\n", Job->Idx);
            assert (Job->Task);
                 (*(Job->Task))();
            delete  Job->Task;
            DBG ("Job #%d, Finished\n", Job->Idx);

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
        if (i < JobArraySize)
            JobArray [i] = Job;
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

void ThreadPool_t::Execute (function <void()> &Task, bool Wait) {
    auto T = new function <void()> (Task);
    Execute (T, Wait);
}

void ThreadPool_t::Execute (function <void()> *Task, bool Wait) {
    JobCtrl *Thr = NULL;
    if (All.size())
        Thr = AllocThread(Wait);
    if (Thr) {
        Thr->Task    = Task;
        Thr->Go();
    } else {
        (*Task)();
        delete Task;
    }
}
