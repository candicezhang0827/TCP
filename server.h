#ifndef _SERVER_H_
#define _SERVER_H_

#include "packet.h"

#include <string>
#include <vector>
#include <list>

#include <poll.h>

//typedef std::pair<Header, std::vector<char> > DataPacket;
//typedef std::list<DataPacket> Buffer;
//typedef Buffer::iterator BuffIter;

class Server {
public:
    unsigned int port;
    int max_packet_size;    
    int max_seq_number;
    
    int sockfd;
    int sigfd;
    int retrans_timerfd; // retransmission timer
    int timeout_timerfd; // timeout timer (to close the connection)
    
    int client_id; // id of client
    
    struct pollfd fds[4];
    
    struct itimerspec RTO; 
    struct itimerspec time_out;
    
    Server(int port, int max_packet_size, int max_seq_number);
    
    void listen();

private:
    int write_buffer_to_file(const Buffer& buffer);
    
    void write_interrupt_to_file();

    void release_resources();
    
    void write_syn_ack_packet(std::vector<char>& packet, Header& header, int& seq_number, 
            int ack_number);
    
    void write_ack_packet(std::vector<char>& packet, Header& header, int seq_number, 
            int ack_number); 
    
    void write_fin_ack_packet(std::vector<char>& packet, Header& header, int& seq_number, 
            int ack_number);

    //void write_fin_packet(std::vector<char>& packet, Header& header, int& seq_number);

    int recv_data_to_buffer(struct sockaddr_in& client_addr, 
            std::vector<char>& in_packet, Header& in_header, 
            std::vector<char>& out_packet, Header& out_header, 
            std::list<DataPacket>& buffer, int seq_number, int expect_seq_number);
    
    void move_iter_forward(Buffer& buffer, BuffIter& inorder_iter, int& ack_number);
    
    void insert_packet_to_buffer(Buffer& buffer, BuffIter& inorder_iter, 
            const std::vector<char>& in_packet, const Header& in_header);
    
    void catch_signal();
    
    void close_connection(struct sockaddr_in& client_addr, std::vector<char>& in_packet, 
            Header& in_header, std::vector<char>& out_packet, Header& out_header, int seq_number);

    void hand_shaking(struct sockaddr_in& client_addr, std::vector<char>& ack_packet, 
            Header& ack_header);
};

#endif
