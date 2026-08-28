#include "version.h"

#include <sstream>

#define VERSION_MAJOR 0
#define VERSION_MINOR 12
#define VERSION_PATCH 0

std::string VersionGetString() {
    std::stringstream ss;
    ss << "v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << " " << VERSION_GIT_BRANCH << ":" << VERSION_GIT_SHORTHASH;
    return ss.str();
}

const char* VersionGetGitHash() {
    return VERSION_GIT_FULLHASH;
}

const char* VersionGetBuildTime() {
    return VERSION_BUILDTIME;
}
