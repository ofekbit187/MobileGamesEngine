#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mge/core/io.h"
#include "test_framework.h"

using namespace mge;

namespace {

// Writes a 64 KiB file where byte i == (i % 251), returns its path.
std::string makePatternFile() {
    std::string path = "/tmp/mge_io_test.bin";
    if (const char* tmpdir = getenv("TMPDIR")) path = std::string(tmpdir) + "/mge_io_test.bin";
    FILE* f = fopen(path.c_str(), "wb");
    for (int i = 0; i < 64 * 1024; ++i) fputc(i % 251, f);
    fclose(f);
    return path;
}

}  // namespace

MGE_TEST(io_reads_correct_bytes_at_offset) {
    const std::string path = makePatternFile();
    AsyncIO io;

    uint8_t buffer[1024];
    IoRequest request;
    request.path = path.c_str();
    request.offset = 1000;
    request.dest = buffer;
    request.size = sizeof(buffer);
    MGE_CHECK(io.submit(&request));
    io.drain();

    MGE_CHECK(request.status() == IoStatus::Done);
    MGE_CHECK(request.bytesRead == sizeof(buffer));
    bool contentOk = true;
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != static_cast<uint8_t>((1000 + i) % 251)) contentOk = false;
    }
    MGE_CHECK(contentOk);
}

MGE_TEST(io_short_read_at_eof_and_missing_file) {
    const std::string path = makePatternFile();
    AsyncIO io;

    // Read straddling EOF: Done with a short byte count.
    uint8_t buffer[4096];
    IoRequest atEof;
    atEof.path = path.c_str();
    atEof.offset = 64 * 1024 - 100;
    atEof.dest = buffer;
    atEof.size = sizeof(buffer);
    MGE_CHECK(io.submit(&atEof));

    // Missing file: Failed with an error code.
    IoRequest missing;
    missing.path = "/nonexistent/mge/file.bin";
    missing.dest = buffer;
    missing.size = 16;
    MGE_CHECK(io.submit(&missing));

    io.drain();
    MGE_CHECK(atEof.status() == IoStatus::Done);
    MGE_CHECK(atEof.bytesRead == 100);
    MGE_CHECK(missing.status() == IoStatus::Failed);
    MGE_CHECK(missing.errorCode != 0);
}

namespace {
struct CompletionLog {
    int order[8];
    std::atomic<int> next{0};
};
void logCompletion(IoRequest* request, void* user) {
    auto* log = static_cast<CompletionLog*>(user);
    log->order[log->next.fetch_add(1)] = static_cast<int>(request->offset);
}
}  // namespace

MGE_TEST(io_priority_order) {
    const std::string path = makePatternFile();
    CompletionLog log;

    uint8_t buffers[4][64];
    IoRequest requests[4];
    const IoPriority priorities[4] = {IoPriority::Low, IoPriority::Normal, IoPriority::Critical,
                                      IoPriority::High};
    // offset doubles as the request's identity in the completion log.
    for (int i = 0; i < 4; ++i) {
        requests[i].path = path.c_str();
        requests[i].offset = static_cast<uint64_t>(i);
        requests[i].dest = buffers[i];
        requests[i].size = sizeof(buffers[i]);
        requests[i].priority = priorities[i];
        requests[i].onComplete = logCompletion;
        requests[i].user = &log;
    }

    AsyncIO io;
    io.pause();  // hold the worker so all four queue up before any runs
    for (auto& request : requests) MGE_CHECK(io.submit(&request));
    io.resume();
    io.drain();

    // Strict priority order: Critical(2), High(3), Normal(1), Low(0).
    MGE_CHECK(log.next.load() == 4);
    MGE_CHECK(log.order[0] == 2);
    MGE_CHECK(log.order[1] == 3);
    MGE_CHECK(log.order[2] == 1);
    MGE_CHECK(log.order[3] == 0);
}

MGE_TEST(io_refuses_when_full_and_malformed) {
    AsyncIO io;
    io.pause();

    const std::string path = makePatternFile();
    uint8_t buffer[16];
    static IoRequest requests[AsyncIO::kQueueCapacity + 1];
    size_t accepted = 0;
    for (auto& request : requests) {
        request.path = path.c_str();
        request.dest = buffer;
        request.size = sizeof(buffer);
        request.priority = IoPriority::Low;
        if (io.submit(&request)) ++accepted;
    }
    MGE_CHECK(accepted == AsyncIO::kQueueCapacity);  // one refused: ring full

    IoRequest malformed;  // no path/dest/size
    MGE_CHECK(!io.submit(&malformed));

    io.resume();
    io.drain();
    MGE_CHECK(requests[0].status() == IoStatus::Done);
}
