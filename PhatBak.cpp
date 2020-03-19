#include "Types.h"
#include "Create.h"
#include "Extract.h"
#include "LiveFile.h"
#include "Logging.h"
#include "Opts.h"
#include "Utils.h"
#include "ThreadPool.h"

#include <stdio.h>

int main (int argc, const char **argv) {
    try {
        O.ParseCmdLine (argc, argv);

        ThreadPool.AddThreads (O.NumThreads);

        if (O.Operation == Opts::DoCreate) {
            Create *C = new Create;
            C->DoCreate ();
            delete C;
        } else if (O.Operation == Opts::DoExtract) {
            Extract *E = new Extract;
            E->DoExtract ();
            delete E;
        }
    }

    // handle exceptions
    catch (const char *msg) {
        fprintf (stderr, "%s\n", msg);
        return 1;
    }
    catch (PB_Exception &PBE) {
        PBE.Handle();
    }

    return 0;
}
