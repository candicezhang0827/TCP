#ifndef _UTILS_H_
#define _UTILS_H_
#include "packet.h"
#include <string>
#include <vector>
#include <sys/timerfd.h>


#define DEBUG(fmt, ...) nop((fmt), ##__VA_ARGS__); 
#define INFO(fmt, ...) info((fmt), ##__VA_ARGS__);
#define ERR(fmt, ...) err((fmt), ##__VA_ARGS__);
#define FATAL(fmt, ...) fatal((fmt), ##__VA_ARGS__);

void print_sys_error(const std::string& extra_info);

void reset_timer(int timerfd, const struct itimerspec& new_time);

int recv_packet(int sockfd, struct sockaddr_in& addr, std::vector<char>& packet, Header& header, int max_packet_size);

int send_packet(int socketfd, const struct sockaddr_in& addr, const std::vector<char>& packet);

void print_log(const std::string& prefix, const Header& header, int cwnd, int ssthresh, bool dup);

void print_log_from_packet(const std::string& prefix, const std::vector<char>& packet, int cwnd, 
        int ssthresh, bool dup);

void print_buffer(const Buffer& buffer);

void debug(const char* fmt, ...);

void info(const char* fmt, ...);

void err(const char* fmt, ...);

void fatal(const char* fmt, ...);

void nop(const char* fmt, ...);

#endif
