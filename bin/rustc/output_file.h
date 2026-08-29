#pragma once

#include "output.h"

#include <std/ios/out_buf.h>
#include <std/ios/out_fd.h>
#include <std/sys/fd.h>

#include <string>

class OutputFile final: public stl::ZeroCopyOutput {
    stl::ScopedFD fd_;
    stl::FDRegular output_;
    stl::OutBuf buffer_;

    void* imbueImpl(size_t* len) override;
    void commitImpl(size_t len) override;
    void flushImpl() override;
    void finishImpl() override;

public:
    explicit OutputFile(const char* path);
    explicit OutputFile(const std::string& path);
    ~OutputFile() override;

    void close();
};
