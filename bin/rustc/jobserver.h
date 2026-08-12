#pragma once

/*
* mrustc common tools
* - by John Hodge (Mutabah)
*
* tools/common/jobserver.h
* - An interface to (or emulation of) make's jobserver
*/
#include <memory>

class JobServer {
public:
    virtual ~JobServer();

    /// <summary>
    /// Create a jobserver instance (client, or server if `server_jobs` is non-zero and there isn't already a server)
    /// </summary>
    /// <param name="server_jobs">Number of downstream job slots to expose</param>
    /// <returns></returns>
    static ::std::unique_ptr<JobServer> create(size_t serverJobs);

    virtual bool takeOne(unsigned long timeoutMs) = 0;
    virtual void returnOne() = 0;
};
