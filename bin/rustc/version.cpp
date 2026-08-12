#include "version.h"
#include <sstream>

#define VERSION_MAJOR 0
#define VERSION_MINOR 12
#define VERSION_PATCH 0

unsigned int giVersionMajor = VERSION_MAJOR;
unsigned int giVersionMinor = VERSION_MINOR;
unsigned int giVersionPatch = VERSION_PATCH;
bool gbVersionGitDirty = VERSION_GIT_ISDIRTY;
const char* gsVersionGitHash = VERSION_GIT_FULLHASH;
const char* gsVersionGitShortHash = VERSION_GIT_SHORTHASH;
const char* gsVersionBuildTime = VERSION_BUILDTIME;

::std::string VersionGetString() {
    ::std::stringstream ss;
    ss << "v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << " " << VERSION_GIT_BRANCH << ":" << VERSION_GIT_SHORTHASH;
    return ss.str();
}
