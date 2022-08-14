#ifndef APPSTATE_H
#define APPSTATE_H

#include <fstream>

#include "../workspace.hpp"

struct AppState {
    Workspace workspace;
    bool verbose;
    bool use_logfile;
    std::ofstream logfile_stream;
};

#endif
