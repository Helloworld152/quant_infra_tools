#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool parsePort(const char *text, int &port) {
    char *end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (!text || *text == '\0' || *end != '\0' || value <= 0 || value > 65535) {
        return false;
    }
    port = static_cast<int>(value);
    return true;
}

bool parseIPv4(const std::string &ip, in_addr &addr) {
    return ::inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

std::string hexPreview(const unsigned char *data, std::size_t len, std::size_t maxBytes) {
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

void printUsage(const char *prog) {
    std::cerr
        << "Usage: " << prog << " <multicast_ip> <port> [bind_ip] [source_ip]\n"
        << "Example (ASM): " << prog << " 239.10.10.10 12345 0.0.0.0\n"
        << "Example (IGMPv3 SSM): " << prog << " 232.10.10.10 12345 192.168.1.10 192.168.1.20\n";
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 5) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string multicastIp = argv[1];
    const std::string bindIp = argc >= 4 ? argv[3] : "0.0.0.0";
    const bool useIgmpV3 = argc == 5;
    const std::string sourceIp = useIgmpV3 ? argv[4] : "";
    int port = 0;
    if (!parsePort(argv[2], port)) {
        std::cerr << "invalid port: " << argv[2] << std::endl;
        return 1;
    }

    in_addr multicastAddr {};
    in_addr interfaceAddr {};
    in_addr sourceAddr {};
    if (!parseIPv4(multicastIp, multicastAddr)) {
        std::cerr << "invalid multicast ip: " << multicastIp << std::endl;
        return 1;
    }
    if (!IN_MULTICAST(ntohl(multicastAddr.s_addr))) {
        std::cerr << "ip is not a multicast address: " << multicastIp << std::endl;
        return 1;
    }
    if (!parseIPv4(bindIp, interfaceAddr)) {
        std::cerr << "invalid bind ip: " << bindIp << std::endl;
        return 1;
    }
    if (useIgmpV3 && !parseIPv4(sourceIp, sourceAddr)) {
        std::cerr << "invalid source ip: " << sourceIp << std::endl;
        return 1;
    }

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 2;
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        std::perror("setsockopt(SO_REUSEADDR)");
        ::close(fd);
        return 2;
    }

    sockaddr_in localAddr {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(static_cast<uint16_t>(port));
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&localAddr), sizeof(localAddr)) != 0) {
        std::perror("bind");
        ::close(fd);
        return 2;
    }

    if (useIgmpV3) {
        ip_mreq_source membership {};
        membership.imr_multiaddr = multicastAddr;
        membership.imr_interface = interfaceAddr;
        membership.imr_sourceaddr = sourceAddr;
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
            std::perror("setsockopt(IP_ADD_SOURCE_MEMBERSHIP)");
            ::close(fd);
            return 2;
        }
    } else {
        ip_mreq membership {};
        membership.imr_multiaddr = multicastAddr;
        membership.imr_interface = interfaceAddr;
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
            std::perror("setsockopt(IP_ADD_MEMBERSHIP)");
            ::close(fd);
            return 2;
        }
    }

    std::cout << "joined multicast group " << multicastIp
              << ":" << port
              << " via interface " << bindIp;
    if (useIgmpV3) {
        std::cout << " with IGMPv3 source filter " << sourceIp;
    }
    std::cout << ", waiting for packets" << std::endl;

    unsigned char buffer[65536];
    sockaddr_in peerAddr {};
    socklen_t peerLen = sizeof(peerAddr);
    ssize_t received = ::recvfrom(
        fd,
        buffer,
        sizeof(buffer),
        0,
        reinterpret_cast<sockaddr *>(&peerAddr),
        &peerLen);

    if (received < 0) {
        std::perror("recvfrom");
        ::close(fd);
        return 2;
    }

    char peerIp[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &peerAddr.sin_addr, peerIp, sizeof(peerIp));

    std::cout << "received multicast packet: bytes=" << received
              << ", from=" << peerIp << ":" << ntohs(peerAddr.sin_port)
              << ", preview=" << hexPreview(buffer, static_cast<std::size_t>(received), 16)
              << std::endl;

    ::close(fd);
    return 0;
}
