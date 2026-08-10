/*
 * mrustc common code
 * - by John Hodge (Mutabah)
 *
 * tools/common/path.cpp
 * - Generic representation of a filesystem path
 */
#include "path.h"
#if _WIN32
    #include <Windows.h>
#else
    #include <unistd.h> // getcwd/chdir
    #include <limits.h> // PATH_MAX
#endif

helpers::path::path(const char* s)
    : m_str(s)
{
    // 1. Normalise path separators to the system specified separator
    for (size_t i = 0; i < m_str.size(); i++) {
        if (m_str[i] == '/' || m_str[i] == '\\') {
            m_str[i] = SEP;
        }
    }

    // 2. Remove any trailing separators
    if (!m_str.empty()) {
        while (!m_str.empty() && m_str.back() == SEP) {
            m_str.pop_back();
        }
        if (m_str.empty()) {
            m_str.push_back(SEP);
        }
    } else {
        throw ::std::runtime_error("Empty path being constructed");
    }
}

helpers::path helpers::path::to_absolute() const {
    if (!this->is_valid()) {
        throw ::std::runtime_error("Calling to_absolute() on an invalid path");
    }

    if (this->m_str[0] == SEP) {
        return *this;
    }

#if _WIN32
    char cwd[1024];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        throw ::std::runtime_error("Calling getcwd() failed in path::to_absolute()");
    }
#endif
    auto rv = path(cwd);
    for (auto comp : *this) {
        if (comp == ".")
            ;
        else if (comp == "..") {
            rv.pop_component();
        } else {
            rv /= comp;
        }
    }
#if _WIN32
#else
#endif
    return rv;
}

helpers::path helpers::path::normalise() const {
    path rv;
    rv.m_str.reserve(m_str.size() + 1);

    for (auto comp : *this) {
        if (comp == ".") {
            // Ignore.
        } else if (comp == "..") {
            // If the path is empty, OR the last element is a "..", push the element
            if (rv.m_str.empty() || (rv.m_str.size() == 3 && rv.m_str[0] == '.' && rv.m_str[1] == '.' && rv.m_str[2] == SEP) || (rv.m_str.size() > 4 && *(rv.m_str.end() - 4) == SEP && *(rv.m_str.end() - 3) == '.' && *(rv.m_str.end() - 2) == '.' && *(rv.m_str.end() - 1) == SEP)) {
                // Push
                rv.m_str += comp;
                rv.m_str += SEP;
            } else {
                rv.m_str.pop_back();
                auto pos = rv.m_str.find_last_of(SEP);
                if (pos == ::std::string::npos) {
                    rv.m_str.resize(0);
                } else if (pos == 0) {
                    // Keep.
                } else {
                    rv.m_str.resize(pos + 1);
                }
            }
        } else {
            rv.m_str += comp;
            rv.m_str += SEP;
        }
    }
    rv.m_str.pop_back();
    return rv;
}


void helpers::path::ComponentsIter::operator++() {
    if (end == p.m_str.size()) {
        pos = end;
    } else {
        pos = end + 1;
        end = p.m_str.find(SEP, pos);
        if (end == ::std::string::npos) {
            end = p.m_str.size();
        }
    }
}
