#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "packet.h"
#include <vector>
#include <string>
#include <deque>
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <sys/timerfd.h>
#include <poll.h>

class Client {
private:
    int cwnd; // cwnd should be double, for cogestion avoidance
    int max_cwnd;
    int ssthresh;
    int MSS;

    int max_seq_number;
    int max_packet_size;
    
    struct itimerspec RTO; 
    struct itimerspec time_out;

    int sockfd;  // socket
    int retrans_timerfd; // retransmission timer
    int timeout_timerfd; // timeout timer (to close the connection)
    int sigfd; // catch the signal
    struct pollfd fds[4];
    struct sockaddr_in server_addr;
    
    void hand_shaking(const std::vector<char>& packet, std::vector<char>& reply, 
            Header& header, int expect_ack);
    
    void catch_signal();
    
    void release_resources();
    
    //void state_transition(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS, int event);
    
    void new_ack_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS);

    bool dup_ack_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS); 

    void timeout_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS);

    void rearrange_queue(std::deque<int>& inflight_packet_bytes, int& bytes_inflight, size_t& idx, 
            int cwnd);

    void send_message(const std::vector<char>& message);
    
    void send_packets_in_window(int last_unacked_seq, const std::vector<std::vector<char> >& packets, 
            std::vector<char>& in_packet, Header& in_header);
    
    void close_connection(std::vector<char>& in_packet, Header& in_header, 
            std::vector<char>& out_packet, Header& out_header, int& seq_number); 

    void write_syn_packet(std::vector<char>& packet, Header& header, int& seq_number);
    
    void write_ack_packet(const char* message, int length, std::vector<char>& packet, 
            Header& header, int& seq_number, int ack_number);
    
    void write_fin_packet(std::vector<char>& packet, Header& header, int& seq_number);
    
    void write_fin_ack_packet(std::vector<char>& packet, Header& header, int seq_number, 
            int ack_number);
    
    void write_data_packets(const std::vector<char>& message, 
            std::vector<std::vector<char> >& packets, std::vector<Header>& headers, 
            int& seq_number, int ack_number);
public:
    Client(const std::string& server_addr, int server_port, int max_seq_number, int max_packet_size, 
            int cwnd, int max_cwnd, int ssthresh, int MSS); 
    
    void send_file(const std::string& file_path);
};

#endif
