#include "pcap_compat.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/time.h>

namespace {

struct CaptureResult {
    bool gotPacket = false;
    unsigned int packetLen = 0;
    std::string preview;
};

bool parseTimeout(const char *text, int &timeoutSec) {
    char *end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (!text || *text == '\0' || *end != '\0' || value <= 0 || value > 3600) {
        return false;
    }
    timeoutSec = static_cast<int>(value);
    return true;
}

std::string hexPreview(const u_char *data, std::size_t len, std::size_t maxBytes) {
    std::ostringstream oss;
    std::size_t previewLen = len < maxBytes ? len : maxBytes;
    for (std::size_t i = 0; i < previewLen; ++i) {
        if (i != 0) {
            oss << ' ';
        }
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned int>(data[i]);
    }
    if (len > maxBytes) {
        oss << " ...";
    }
    return oss.str();
}

void onPacket(u_char *user, const struct pcap_pkthdr *header, const u_char *bytes) {
    CaptureResult *result = reinterpret_cast<CaptureResult *>(user);
    if (result->gotPacket) {
        return;
    }
    result->gotPacket = true;
    result->packetLen = header ? header->len : 0U;
    result->preview = hexPreview(bytes, header ? header->caplen : 0U, 16);
}

void printUsage(const char *prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " live <interface> [timeout_sec]\n"
        << "  " << prog << " file <pcap_file>\n"
        << "Examples:\n"
        << "  " << prog << " live eth0 5\n"
        << "  " << prog << " file sample.pcap\n";
}

int runLive(const char *device, int timeoutSec) {
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *handle = pcap_open_live(device, 262144, 1, 1000, errbuf);
    if (!handle) {
        std::cerr << "pcap_open_live failed: " << errbuf << std::endl;
        return 2;
    }

    std::cout << "capturing on interface " << device
              << ", waiting up to " << timeoutSec << "s" << std::endl;

    CaptureResult result;
    timeval start {};
    gettimeofday(&start, nullptr);

    while (true) {
        int rc = pcap_dispatch(handle, 1, onPacket, reinterpret_cast<u_char *>(&result));
        if (rc == PCAP_ERROR || rc == PCAP_ERROR_BREAK) {
            std::cerr << "pcap_dispatch failed: " << pcap_geterr(handle) << std::endl;
            pcap_close(handle);
            return 2;
        }
        if (result.gotPacket) {
            std::cout << "captured live packet: bytes=" << result.packetLen
                      << ", preview=" << result.preview << std::endl;
            pcap_close(handle);
            return 0;
        }

        timeval now {};
        gettimeofday(&now, nullptr);
        long elapsed = now.tv_sec - start.tv_sec;
        if (elapsed >= timeoutSec) {
            std::cerr << "timeout waiting for live packet" << std::endl;
            pcap_close(handle);
            return 3;
        }
    }
}

int runFile(const char *path) {
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *handle = pcap_open_offline(path, errbuf);
    if (!handle) {
        std::cerr << "pcap_open_offline failed: " << errbuf << std::endl;
        return 2;
    }

    CaptureResult result;
    int rc = pcap_dispatch(handle, 1, onPacket, reinterpret_cast<u_char *>(&result));
    if (rc == PCAP_ERROR || rc == PCAP_ERROR_BREAK) {
        std::cerr << "pcap_dispatch failed: " << pcap_geterr(handle) << std::endl;
        pcap_close(handle);
        return 2;
    }
    if (!result.gotPacket) {
        std::cerr << "no packet found in pcap file" << std::endl;
        pcap_close(handle);
        return 3;
    }

    std::cout << "captured file packet: bytes=" << result.packetLen
              << ", preview=" << result.preview << std::endl;
    pcap_close(handle);
    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string mode = argv[1];
    if (mode == "live") {
        int timeoutSec = 5;
        if (argc >= 4 && !parseTimeout(argv[3], timeoutSec)) {
            std::cerr << "invalid timeout_sec: " << argv[3] << std::endl;
            return 1;
        }
        return runLive(argv[2], timeoutSec);
    }
    if (mode == "file") {
        return runFile(argv[2]);
    }

    printUsage(argv[0]);
    return 1;
}
