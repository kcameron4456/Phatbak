#include "BlockList.h"
#include "Logging.h"
#include "Opts.h"

#include <map>
#include <random>
#include <iterator>
#include <inttypes.h>

BlockList                     List ("Test");
map <i64, bool>               Allocated, UnAllocated;
default_random_engine         generator (12345);
int                           ComIdx;
int                           PreAllocSize = 100, PreAllocMax = 1000;

void DoAlloc () {
    i64 Idx = List.Alloc();
    DBG ("%d: Allocated : %" PRId64 "\n", ComIdx, Idx);

    if (Allocated.find(Idx) != Allocated.end())
        THROW_PBEXCEPTION ("Alloc returned already allocated index: %d", Idx);

     Allocated        [Idx] = 1;
    UnAllocated.erase (Idx);
}

void DoFree () {
    if (!Allocated.size())
        return;
    uniform_int_distribution<int> distribution (0,Allocated.size()-1);
    i64 ToFreeDelta = distribution (generator);

    auto Itr = Allocated.begin();
    advance (Itr, (long unsigned)ToFreeDelta);
    i64 ToFree = Itr->first;

    if (!Allocated [ToFree])
        THROW_PBEXCEPTION ("Selected bad index to free: %" PRId64, ToFree);
    DBG ("%d: Freeing   : %" PRId64 "\n", ComIdx, ToFree);

    List.Free         (ToFree);
      Allocated.erase (ToFree);
    UnAllocated       [ToFree] = 1;
}

void DoMark () {
    if (!UnAllocated.size())
        return;
    uniform_int_distribution<int> distribution (0,UnAllocated.size()-1);
    i64 ToMarkDelta = distribution (generator);

    auto Itr = UnAllocated.begin();
    advance (Itr, (long unsigned)ToMarkDelta);
    i64 ToMark = Itr->first;
    DBG ("%d: Marking   : %" PRId64 "\n", ComIdx, ToMark);

    List.MarkAllocated  (ToMark);
      Allocated         [ToMark] = 1;
    UnAllocated.erase   (ToMark);
}

void DoTestSize () {
    i64 ListSize  = List.CountAllocated();
    i64 AllocSize = Allocated.size();
    DBG ("%d: ListSize = %d, AllocSize=%d\n", ComIdx, (int)ListSize, (int)AllocSize);
    if (ListSize != AllocSize)
        THROW_PBEXCEPTION ("Allocation counts don't match:  Found:%" PRId64 " Expected:%" PRId64, ListSize, AllocSize);
}

int main (int argc, char **argv) {
    O.BlockNumModulus = 100;
    O.DebugPrint = 0;

    int count = 1000000;
    for (int i = 1; i < argc; i++) {
        if (string ("-c") == argv[i])
            count = stoi (string (argv[++i]), NULL, 10);
        if (string ("-p") == argv[i])
            PreAllocSize = stoi (string (argv[++i]), NULL, 10);
        if (string ("-m") == argv[i])
            PreAllocMax = stoi (string (argv[++i]), NULL, 10);
        else if (string ("-d") == argv[i])
            O.DebugPrint = 1;
    }

    generator.seed (1234);

    for (int i = 0; i < PreAllocSize; i++) {
        uniform_int_distribution<int> distribution(0,PreAllocMax);
        UnAllocated [distribution(generator)] = 1;
    }

    try {
        uniform_int_distribution<int> distribution(0,2);

        for (ComIdx = 1; ComIdx <= count; ComIdx++) {
            int op = distribution(generator);
            switch (op) {
                case 0 : DoAlloc(); break;
                case 1 : DoFree (); break;
                case 2 : DoMark (); break;
                default: THROW_PBEXCEPTION ("Bad Op: %d", op);
            }

            DoTestSize ();
        }
    }

    // handle exceptions
    catch (const char *msg) {
        fprintf (stderr, "Exception: %s\n", msg);
        return 1;
    }
    catch (PB_Exception &PBE) {
        PBE.Handle();
    }
}
