#include "output_file.h"

#include <cerrno>
#include <fcntl.h>
#include <system_error>

using namespace stl;

namespace {
int openOutputFile(const char* path) {
    auto fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), path);
    }
    return fd;
}
}

OutputFile::OutputFile(const char* path)
    : fd_(openOutputFile(path))
    , output_(fd_)
    , buffer_(output_)
{
}

OutputFile::OutputFile(const std::string& path)
    : OutputFile(path.c_str())
{
}

OutputFile::~OutputFile() = default;

void OutputFile::close() {
    buffer_.finish();
    fd_.close();
}

void* OutputFile::imbueImpl(size_t* len) {
    return buffer_.imbue(len);
}

void OutputFile::commitImpl(size_t len) {
    buffer_.commit(len);
}

void OutputFile::flushImpl() {
    buffer_.flush();
}

void OutputFile::finishImpl() {
    buffer_.finish();
}
