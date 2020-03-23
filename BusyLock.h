#ifndef BUSYLOCK_H
#define BUSYLOCK_H

#include <mutex>
#include <condition_variable>
using namespace std;

extern volatile int StopThreads;

#if 1
class BusyLock {
    bool               Busy;
    mutex              Mtx;
    condition_variable CV;

    public:
    BusyLock (bool b = false) {
        Busy = b;
    }
    void WaitIdle () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return !Busy || StopThreads;});
    }
    void WaitIdleAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return !Busy || StopThreads;});
        Busy = 1;
    }
    void WaitBusy () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Busy || StopThreads;});
    }
    void WaitBusyAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Busy || StopThreads;});
        Busy = 0;
    }
    void Notify () {
        CV.notify_all();
    }
    void Post (bool B) {
        unique_lock<mutex> lock(Mtx);
        Busy = B;
        Notify ();
    }
    void PostIdle () {
        Post (0);
    }
    void PostBusy () {
        Post (1);
    }
};

#else
class BusyLock {
    i32                Count;
    mutex              Mtx;
    condition_variable CV;

    public:
    BusyLock (i32 c = 0) {
        Count = c;
    }
    void WaitIdle () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Count==0 || StopThreads;});
    }
    void WaitIdleAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Count==0 || StopThreads;});
        Count = 1;
    }
    void WaitBusy () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Count!=0 || StopThreads;});
    }
    void WaitBusyAndPost () {
        unique_lock<mutex> lock(Mtx);
        CV.wait (lock, [this]{return Count!=0 || StopThreads;});
        Count--;
    }
    void Notify () {
        CV.notify_all();
    }
    void Post (i32 D) {
        unique_lock<mutex> lock(Mtx);
        Count += D;
        if (Count < 0)
            Count = 0;
        Notify ();
    }
    void PostIdle () {
        Post (-1);
    }
    void PostBusy () {
        Post (1);
    }
};
#endif

#endif // BUSYLOCK_H
