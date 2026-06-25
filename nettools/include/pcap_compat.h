#ifndef NETTOOLS_PCAP_COMPAT_H
#define NETTOOLS_PCAP_COMPAT_H

#if __has_include(<pcap/pcap.h>)
#include <pcap/pcap.h>
#elif __has_include(<pcap.h>)
#include <pcap.h>
#else
#include <sys/time.h>
#include <sys/types.h>

typedef unsigned char u_char;
typedef unsigned int bpf_u_int32;

#define PCAP_ERRBUF_SIZE 256
#define PCAP_ERROR -1
#define PCAP_ERROR_BREAK -2

typedef struct pcap pcap_t;

struct pcap_pkthdr {
    struct timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

typedef void (*pcap_handler)(u_char *, const struct pcap_pkthdr *, const u_char *);

extern "C" {
pcap_t *pcap_open_live(const char *device, int snaplen, int promisc, int to_ms, char *errbuf);
pcap_t *pcap_open_offline(const char *fname, char *errbuf);
int pcap_dispatch(pcap_t *p, int cnt, pcap_handler callback, u_char *user);
char *pcap_geterr(pcap_t *p);
void pcap_close(pcap_t *p);
}
#endif

#endif
