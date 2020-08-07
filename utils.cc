#include "utils.h"
#include "packet.h"
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>

#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

void print_sys_error(const std::string& extra_info) {
    char buffer[512] = {0};
    sprintf(buffer, "%s: %s", extra_info.c_str(), strerror(errno));
    ERR("ERROR: %s\n", buffer);
}


void reset_timer(int timerfd, const struct itimerspec& new_time) {
    if (timerfd_settime(timerfd, 0, &new_time, NULL) != 0) {
        print_sys_error("Unable to reset timer");
        exit(EXIT_FAILURE);
    }
}

// print log according to format:
// RECV <SeqNum> <AckNum> <cwnd> <ssthresh> [ACK] [SYN] [FIN]
// SEND <SeqNum> <AckNum> <cwnd> <ssthresh> [ACK] [SYN] [FIN] [DUP]
void print_log(const std::string& prefix, const Header& header, int cwnd, int ssthresh, bool dup) {
    std::string state = "";
    if (header.ack) {
        state = "ACK";
    }
    if (header.syn) {
        state = state + " SYN";
    }
    else if (header.fin) {
        state = state + " FIN";
    }
    std::string format;
    if (dup) {
        assert (prefix == "SEND");
        format = "%s %d %d %d %d %s DUP\n";
    } 
    else {
        format = "%s %d %d %d %d %s\n";   
    }
    INFO(format.c_str(), prefix.c_str(), header.seq_number, header.ack_number, 
            cwnd, ssthresh, state.c_str());
}

void print_log_from_packet(const std::string& prefix, const std::vector<char>& packet, int cwnd, 
        int ssthresh, bool dup) {
    Header header;
    memcpy(&header, packet.data(), sizeof(Header));
    print_log(prefix, header, cwnd, ssthresh, dup);
}

int recv_packet(int sockfd, struct sockaddr_in& addr, std::vector<char>& packet, Header& header, 
        int max_packet_size) {
    // receive all data
    packet.resize(max_packet_size);
    socklen_t addr_size = sizeof(addr);
    int actual_size = recvfrom(sockfd, packet.data(), max_packet_size, MSG_WAITALL, 
            (struct sockaddr*) &addr, &addr_size);
    if (actual_size < 0) {
        // timeout
        return -1;
    }
    packet.resize(actual_size);
    // write header
    memcpy(&header, packet.data(), sizeof(header));
    return 0;

}

int send_packet(int socketfd, const struct sockaddr_in& addr, const std::vector<char>& packet) {
    return sendto(socketfd, packet.data(), packet.size(), 0, 
            (const struct sockaddr*) &addr, sizeof(addr));
}

void print_buffer(const Buffer& buffer) {
    int init_seq = -1;
    int payload = 0;
    bool error = false;
    int count = 20 > buffer.size() ? buffer.size() : 20;
    auto it = std::prev(buffer.end(), count);
    for (; it != buffer.end(); ++it) {
        DEBUG("(%d)->", it->first.seq_number);
        if (init_seq >= 0) {
            int expect = (init_seq + payload) % 25600;
            int actual = it->first.seq_number;
            if (it->first.seq_number < expect - 25600 / 2)
                actual += 25600;
            if (actual < expect)
                error = true;
        }
        init_seq = it->first.seq_number;
        payload = it->second.size() - sizeof(Header);
    }
    /*
    for (auto e : buffer) {
        DEBUG("(%d)->", e.first.seq_number);
        if (init_seq >= 0) {
            int expect = (init_seq + payload) % 25600;
            int actual = e.first.seq_number;
            if (e.first.seq_number < expect - 25600 / 2)
                actual += 25600;
            if (actual < expect)
                error = true;
        }
        init_seq = e.first.seq_number;
        payload = e.second.size() - sizeof(Header);
    }
    */
    DEBUG("ENDL\n");
    if (error)
        exit(EXIT_FAILURE);
}

void debug(const char* fmt, ...) {
    va_list arglist;
    va_start(arglist, fmt);
    //printf("");
    vprintf(fmt, arglist);
    va_end(arglist);
}

void info(const char* fmt, ...) {
    va_list arglist;
    va_start(arglist, fmt);
    vprintf(fmt, arglist);
    va_end(arglist);
}

void err(const char* fmt, ...) {
    va_list arglist;
    va_start(arglist, fmt);
    printf("[ERROR] ");
    vfprintf(stderr, fmt, arglist);
    va_end(arglist);
}

void fatal(const char* fmt, ...) {
    va_list arglist;
    va_start(arglist, fmt);
    printf("[FATAL] ");
    vfprintf(stderr, fmt, arglist);
    va_end(arglist);
}
void nop(const char* fmt, ...) {
    // do nothing
}
