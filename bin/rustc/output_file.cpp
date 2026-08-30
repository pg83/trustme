#include "output_file.h"

#include <std/ios/out_buf.h>
#include <std/ios/out_fd.h>
#include <std/lib/buffer.h>
#include <std/str/builder.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>

#include <fcntl.h>

using namespace stl;

namespace {
int openOutputFile(StringView path) {
    auto fd = open(Buffer(path).cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        Errno().raise(StringBuilder() << StringView(u8"can not open ") << path);
    }
    return fd;
}
}

ZeroCopyOutput* outputFile(ObjPool& pool, StringView path) {
    auto* fd = pool.make<ScopedFD>(openOutputFile(path));
    auto* output = pool.make<FDRegular>(*fd);
    return pool.make<OutBuf>(*output);
}
