#include "version.h"
#include "output.h"

#include <std/str/builder.h>

using namespace stl;

#define VERSION_MAJOR 0
#define VERSION_MINOR 12
#define VERSION_PATCH 0

std::string VersionGetString() {
    StringBuilder out;
    out << StringView("v") << VERSION_MAJOR << StringView(".") << VERSION_MINOR << StringView(".") << VERSION_PATCH << StringView(" ") << StringView(VERSION_GIT_BRANCH) << StringView(":") << StringView(VERSION_GIT_SHORTHASH);
    return std::string(static_cast<const char*>(out.data()), out.length());
}

const char* VersionGetGitHash() {
    return VERSION_GIT_FULLHASH;
}

const char* VersionGetBuildTime() {
    return VERSION_BUILDTIME;
}
