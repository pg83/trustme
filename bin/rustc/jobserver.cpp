/*
* mrustc common tools
* - by John Hodge (Mutabah)
*
* tools/common/jobserver.cpp
* - An interface to (or emulation of) make's jobserver
*/
#include "jobserver.h"
#include <cstring>
#include <cassert>
#include <string>
#include <sstream>
#include <iostream>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <thread>
    #include <vector>

class JobServerClient: public JobServer {
    int fdRead;
    int fdWrite;
    std::vector<uint8_t> heldTokens;
    //::std::semaphore    m_sem;
public:
    JobServerClient(int fdRead, int fdWrite = -1)
        : fdRead(fdRead)
        , fdWrite(fdWrite)
    {
        assert(fdRead >= 0);
    }

    ~JobServerClient() {
        if (fdWrite == -1) {
            close(fdRead);
        }
    }

    bool takeOne(unsigned long timeoutMs) override {
        if (timeoutMs != ~0ul) {
            assert(fdRead >= 0);
            struct timeval timeout;
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fdRead, &fds);
            if (select(fdRead + 1, &fds, nullptr, nullptr, &timeout) != 1) {
                return false;
            }
        }
        uint8_t token;
        int rv = read(fdRead, &token, 1);
        if (rv != 1) {
            perror("JobServer_Client::take_one read");
            return false;
        }
        heldTokens.push_back(token);
        return true;
    }

    void returnOne() override {
        assert(!heldTokens.empty());
        auto t = heldTokens.back();
        heldTokens.pop_back();
        if (write(fdWrite == -1 ? fdRead : fdWrite, &t, 1) != 1) {
            // What can be done if the write fails?
            perror("JobServer_Client write");
        }
    }
};

class JobServerServer: public JobServer {
    class ServerInner {
        ::std::string mPath;
        int wrFd;
        int rdFd;

    public:
        ServerInner(size_t maxJobs)
            : mPath()
            , wrFd(-1)
            , rdFd(-1)
        {
    #if 1 && (_POSIX_C_SOURCE >= 200809L)
            char buf[] = "/tmp/mrustc-jobserverXXXXXX";
            mPath = ::std::string(mkdtemp(buf));
            if (mkfifo((mPath + "/fifo").c_str(), 0600) != 0) {
                perror("JobServer_Server mkfifo");
                throw std::runtime_error("JobServer_Server mkfifo");
            }
            wrFd = open((mPath + "/fifo").c_str(), O_RDWR | O_CLOEXEC);
            if (wrFd < 0) {
                perror("JobServer_Server open");
                throw std::runtime_error("JobServer_Server open");
            }
    #else
            // TODO: For `pipe` it would be nice to propagate it to child processes, but that needs minicargo's `os`
            // support to be happy.
            int pipeFds[2] = {-1, -1};
            if (pipe(pipeFds) != 0) {
                throw std::runtime_error("pipe failed");
            }
            rdFd = pipeFds[0];
            wrFd = pipeFds[1];
    #endif
            for (size_t i = 0; i < maxJobs; i++) {
                uint8_t t = 100;
                if (write(wrFd, &t, 1) != 1) {
                    perror("ServerInner() write");
                }
            }
        }

        ~ServerInner() {
            if (rdFd != -1) {
                close(rdFd);
            }
            close(wrFd);
            if (!mPath.empty()) {
                if (unlink((mPath + "/fifo").c_str()) != 0) {
                    perror("~ServerInner unlink fifo");
                }
                if (rmdir(mPath.c_str()) != 0) {
                    perror("~ServerInner unlink dir");
                }
            }
        }

        int getClientReadFd() const {
            return rdFd == -1 ? wrFd : rdFd;
        }

        int getClientWriteFd() const {
            return wrFd;
        }

        void dumpDesc(::std::ostream& os) const {
            if (rdFd == -1) {
                os << "fifo:" << mPath << "/fifo";
            } else {
                os << rdFd << "," << wrFd;
            }
        }
    };

    ServerInner server;
    JobServerClient client;

public:
    JobServerServer(size_t maxJobs)
        : server(maxJobs)
        , client(server.getClientReadFd(), server.getClientWriteFd())
    {
        ::std::stringstream ss;
        if (const auto* makeflags = getenv("MAKEFLAGS")) {
            ss << makeflags << " ";
        }
        ss << "--jobserver-auth=";
        server.dumpDesc(ss);
        setenv("MAKEFLAGS", ss.str().c_str(), /*overwrite=*/1);
    }

    ~JobServerServer() {
    }

    bool takeOne(unsigned long timeoutMs) override {
        return client.takeOne(timeoutMs);
    }

    void returnOne() override {
        return client.returnOne();
    }
};

::std::unique_ptr<JobServer> JobServer::create(size_t serverJobs) {
    const auto* makeflags = getenv("MAKEFLAGS");

    const char* jobserverAuth = nullptr;
    if (makeflags) {
        const char* const needle = "--jobserver-auth=";
        auto pos = ::std::strstr(makeflags, needle);
        while (pos != nullptr) {
            auto e = pos + ::std::strlen(needle);
            if (pos == makeflags || pos[-1] == ' ') {
                jobserverAuth = e;
            }
            pos = ::std::strstr(e, needle);
        }
    }

    if (jobserverAuth) {
        const auto* p = std::strchr(jobserverAuth, ' ');
        auto len = p ? p - jobserverAuth : strlen(jobserverAuth);
        std::string authStr(jobserverAuth, len);

        // Found a valid jobserver string!
        // - Named pipe: `fifo:PATH`
        if (std::strncmp(authStr.c_str(), "fifo:", 5) == 0) {
            auto fd = open(authStr.c_str() + 5, O_RDWR | O_CLOEXEC);
            if (fd > 0) {
                return ::std::make_unique<JobServerClient>(fd);
            }
        }
        // - Unix pipe pair: `<fd_r>,<fd_w>`
        else {
            int fdR = -1, fdW = -1;
            if (::std::sscanf(authStr.c_str(), "%d,%d", &fdR, &fdW) == 2) {
                if (fdR >= 0 && fdW >= 0) {
                    if (fcntl(fdR, F_GETFL) == -1 || fcntl(fdW, F_GETFL) == -1) {
                        ::std::cerr << "JobServer: Pipe FDs aren't open, likely missing `+` in makefile" << std::endl;
                    } else {
                        return ::std::make_unique<JobServerClient>(fdR, fdW);
                    }
                } else {
                }
            }
        }
    }
    // If no `-j` option is passed to this application, then don't create a jobserver
    if (serverJobs == 0) {
        return nullptr;
    }
    return ::std::make_unique<JobServerServer>(serverJobs);
}

JobServer::~JobServer() {
}
