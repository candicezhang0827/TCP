// project headers
#include "client.h"
#include "utils.h"
// C++ headers
#include <deque>
#include <vector>
#include <algorithm>
// C headers
#include <cstdio> 
#include <cstdlib> 
#include <cstring> 
#include <ctime>
#include <cassert>
// LINUX headers
#include <unistd.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <fcntl.h>


Client::Client(const std::string& server_ip, int server_port, int max_seq_number, 
        int max_packet_size, int cwnd, int max_cwnd, int ssthresh, int MSS) 
    : cwnd(cwnd), max_cwnd(max_cwnd), ssthresh(ssthresh), MSS(MSS), max_seq_number(max_seq_number), 
    max_packet_size(max_packet_size) {

    // initialize UDP socket, support timeout
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        print_sys_error("Unable to initialize UDP socket");
        exit(EXIT_FAILURE); 
    }
    
    // address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    
    // create timer file descriptor, nonblock
    retrans_timerfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
    timeout_timerfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
    struct timespec TTL, TO, zero;
    TTL.tv_sec = 0;
    TTL.tv_nsec = 500000000; // 0.5 sec = 500 ms = 500000 us = 500000000 
    TO.tv_sec = 100;
    TO.tv_nsec = 0;
    zero.tv_sec = 0;
    zero.tv_nsec = 0;
    RTO.it_value = TTL;
    RTO.it_interval = zero;
    time_out.it_value = TO;
    time_out.it_interval = zero;

    // create signal file descriptor
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        print_sys_error("Unable to call sigprocmask");
        exit(EXIT_FAILURE);
    }
    sigfd = signalfd(-1, &mask, 0);
    
    // add files to monitor
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;
    fds[1].fd = retrans_timerfd;
    fds[1].events = POLLIN;
    fds[2].fd = timeout_timerfd;
    fds[2].events = POLLIN;
    fds[3].fd = sigfd;
    fds[3].events = POLLIN;

    // set random seed
    srand(time(0));
    // ready to send and receiver
}

void Client::release_resources() {
    close(sockfd);
    close(retrans_timerfd);
    close(timeout_timerfd);
    close(sigfd);
}

// new ACK arrives
void Client::new_ack_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS) {
    // check whether slow start or congestion avoidance
    if (dup_ack_count >= 3) {
        // if in fast retransmission mode, when meeting a new ACK, set cwnd = ssthresh
        cwnd = ssthresh;
    }
    else if (cwnd >= ssthresh) {
        // congestion avoidance
        cwnd += MSS * MSS / cwnd;    
    }
    else {
        // slow start mode
        cwnd += MSS;
    }
    dup_ack_count = 0;
    cwnd = std::min(cwnd, max_cwnd);
}

// duplicated ACK arrives
bool Client::dup_ack_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS) {
    // duplicated ACK
    dup_ack_count += 1;
    bool should_retransmit = false;
    if (dup_ack_count == 3) {
        // enter fast recovery mode for the first time
        ssthresh = std::max(cwnd / 2, 1024);
        cwnd = ssthresh + 3 * MSS;
        should_retransmit = true;
    }
    else if (dup_ack_count > 3) {
        // receiving more duplicated acks
        cwnd += MSS;
    }
    cwnd = std::min(cwnd, max_cwnd);
    return should_retransmit;
}

// timeout
void Client::timeout_arrives(int& cwnd, int& ssthresh, int& dup_ack_count, int MSS) {
    // timeout
    ssthresh = std::max(cwnd / 2, 1024);
    cwnd = MSS;
    dup_ack_count = 0;
    // 1. retransmit missing packet immediately
    // 2. squeeze out packets from queue
}

void Client::rearrange_queue(std::deque<int>& inflight_packet_bytes, int& bytes_inflight, 
        size_t& idx, int cwnd) {
    // we need to make sure, after calling this function, sum(bytes_inflight) <= cwnd, and 
    // idx is set properly
    
    // if bytes inflight is already smaller than cwnd, direcly return
    if (bytes_inflight <= cwnd) return;

    while (bytes_inflight > cwnd) {
        // pop packets from back of inflight_packet_bytes
        int bytes_of_last_packet = inflight_packet_bytes.back();
        inflight_packet_bytes.pop_back();
        bytes_inflight -= bytes_of_last_packet;
        idx -= 1;
    }
}

void Client::send_file(const std::string& file_path) {
    // open file (as binary) to read
    FILE* file = fopen(file_path.c_str(), "rb");
    if (file == NULL) {
        FATAL("file does not exist: %s\n", file_path.c_str());
        exit(EXIT_FAILURE);
    }
    unsigned long length = 0;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::vector<char> message;
    message.resize(length);
    fread(message.data(), sizeof(char), length, file);
    fclose(file);
    // send message
    send_message(message);
}

// write SYN packet to packet and update seq_number
void Client::write_syn_packet(std::vector<char>& packet, Header& header, int& seq_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.syn = true;
    memcpy(packet.data(), &header, sizeof(header));
    seq_number = (seq_number + 1) % max_seq_number;
}

void Client::write_ack_packet(const char* message, int length, std::vector<char>& packet, Header& header, 
        int& seq_number, int ack_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header)+length);
    header.seq_number = seq_number;
    header.ack_number = ack_number;
    header.ack = true;
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data()+sizeof(header), message, length * sizeof(char));
    seq_number = (seq_number + length) % max_seq_number;
}

void Client::write_fin_packet(std::vector<char>& packet, Header& header, int& seq_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.fin = true;
    memcpy(packet.data(), &header, sizeof(header));
    seq_number = (seq_number + 1) % max_seq_number;
}

// ACK packet to answer FIN packet, no payload, do not increase seq_number
void Client::write_fin_ack_packet(std::vector<char>& packet, Header& header, int seq_number, 
        int ack_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.ack_number = ack_number;
    header.ack = true;
    memcpy(packet.data(), &header, sizeof(header));
    // do not change seq_number
}

void Client::write_data_packets(const std::vector<char>& message, 
        std::vector<std::vector<char> >& packets, std::vector<Header>& headers, int& seq_number, 
        int ack_number) {
    std::vector<char> packet;
    Header header;
    int max_payload_size = max_packet_size - sizeof(Header);
    // add payloads
    for (size_t bytes_sent = 0; bytes_sent != message.size();) {
        int remaining = message.size() - bytes_sent;
        int payload = 0;
        if (remaining >= max_payload_size) {
            payload = max_payload_size;
        }
        else {
            payload = remaining;
        }
        write_ack_packet(message.data()+bytes_sent, payload, packet, header, seq_number, ack_number);
        headers.push_back(header);
        packets.push_back(packet);
        bytes_sent += payload;
    }
}


void Client::catch_signal() {
    struct signalfd_siginfo fdsi;
    int s = read(sigfd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
        print_sys_error("Unable to read signalfd");
        release_resources();
        exit(EXIT_FAILURE);
    }
    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGQUIT || fdsi.ssi_signo == SIGTERM) {
        // caught termination signal
        FATAL("WARN: caught termination signal, exiting...\n");
        // close socket
        release_resources();
        exit(0);
    }
    else {
        // unknown signal, ignore?
        ERR("ERR: caught unknown signal, ignore\n");
    }
}

// handshaking
void Client::hand_shaking(const std::vector<char>& packet, std::vector<char>& reply, 
        Header& header, int expect_ack) {
    bool ok = false;
    // reset timeout timer
    reset_timer(timeout_timerfd, time_out);
    for (;!ok;) {
        // send SYN packet
        int bytes_sent = send_packet(sockfd, server_addr, packet);
        if (bytes_sent < 0) {
            ERR("ERR: fail to sent packet\n");
        }
        print_log_from_packet("SEND", packet, cwnd, ssthresh, false); 
        // reset retransmission timer
        reset_timer(retrans_timerfd, RTO);
        for (;;) {
            // wait for response or timeout or signal
            int val = poll(fds, 4, -1);
            if (val < 0) {
                // an error occurs
                print_sys_error("Bad poll calling");
                release_resources();
                exit(EXIT_FAILURE);
            }
            // process polling result
            if (fds[0].revents != 0) {
                // socket is readable
                recv_packet(sockfd, server_addr, reply, header, max_packet_size);
                print_log("RECV", header, cwnd, ssthresh, false);
                if (header.ack_number != expect_ack) {
                    // wrong ack_number
                    ERR("ERR: wrong ack_number, will be ignored");
                    continue; 
                }
                // good ack, return
                ok = true;
                break;
            }
            else if (fds[1].revents != 0) {
                // timeout, resent packet and reset timer
                ERR("Retransmission timeout!\n");
                break; 
            }
            else if (fds[2].revents != 0) {
                // close socket and exit with nonzero code
                release_resources();
                exit(EXIT_FAILURE);
            }
            else if (fds[3].revents != 0) {
                // signal captured
                catch_signal();
            }
        } // end inner loop
    } // end outer loop
    return;
}

// send all packets with moving window
void Client::send_packets_in_window(int last_unacked_seq, 
        const std::vector<std::vector<char> >& packets, std::vector<char>& in_packet, 
        Header& in_header) {
    int bytes_inflight = 0;
    int bytes_received = 0;
    std::deque<int> inflight_packet_bytes;
    // reset timeout timer for the first time
    reset_timer(timeout_timerfd, time_out);
    reset_timer(retrans_timerfd, RTO);
    // Goals: 1. after sending packets, maintain bytes_inflight + bytes_received unchanged
    //        2. sum(inflight_packet_bytes) = bytes_inflight;
    //        3. idx is always the index of packet going to be sent
    assert (sizeof(Header) == 12);
    int dup_ack_count = 0;
    for (size_t idx = 0; (idx != packets.size()) || (bytes_inflight != 0) ;) {
        int next_packet_size = 0;
        if (idx != packets.size()) {
            next_packet_size = packets[idx].size() - sizeof(Header);
        }
        
        while (next_packet_size != 0 && bytes_inflight + next_packet_size <= cwnd) {
            // good to go
            const std::vector<char>& packet_to_go = packets[idx];
            send_packet(sockfd, server_addr, packet_to_go);
            inflight_packet_bytes.push_back(next_packet_size);
            bytes_inflight += next_packet_size;
            print_log_from_packet("SEND", packet_to_go, cwnd, ssthresh, false);
            idx += 1;
            if (idx == packets.size()) {
                // no more packets to send
                break;
            }
            next_packet_size = packets[idx].size() - sizeof(Header);
        }
        // reset retransmission timer
        //reset_timer(retrans_timerfd, RTO);
        int val = poll(fds, 4, -1);
        if (val < 0) {
            // an error occurs
            print_sys_error("Bad poll calling");
            release_resources();
            exit(EXIT_FAILURE);
        }
        if (fds[0].revents != 0) {
            recv_packet(sockfd, server_addr, in_packet, in_header, max_packet_size);
            print_log("RECV", in_header, cwnd, ssthresh, false);
            int ack_number = in_header.ack_number;
            if (ack_number < last_unacked_seq - max_seq_number / 2) {
                ack_number += max_seq_number;
            }
            if (ack_number > last_unacked_seq) {
                // new ACK arrives, reset retransmission timer
                reset_timer(retrans_timerfd, RTO);
                int total_bytes_received = ack_number - last_unacked_seq;
                bytes_inflight -= std::min(bytes_inflight, total_bytes_received);
                bytes_received += total_bytes_received;
                last_unacked_seq = in_header.ack_number;
                // pop out some inflight packets
                // TODO check if we need < 0 instead
                while (total_bytes_received != 0 && !inflight_packet_bytes.empty()) {
                    DEBUG("Poping out packets\n");
                    int bytes = inflight_packet_bytes.front();
                    inflight_packet_bytes.pop_front();
                    total_bytes_received -= bytes;
                }
                // in extreme case (w/ cumulative ACK), the ACK number may be very large 
                // and exceed the current queue, in this case, move window forward, so that
                // total_bytes_received == 0
                while (total_bytes_received != 0) {
                    DEBUG("Have very long ack, total_bytes_received: %d\n", total_bytes_received);
                    DEBUG("Bytes received: %d\n", bytes_received);
                    int bytes = packets[idx].size() - sizeof(Header);
                    total_bytes_received -= bytes;
                    idx += 1;
                }
                // ack new packets
                new_ack_arrives(cwnd, ssthresh, dup_ack_count, MSS);
            }
            else {
                // Duplicated ACK, ignore here, 
                bool should_retransmit = dup_ack_arrives(cwnd, ssthresh, dup_ack_count, MSS);
                if (should_retransmit) {
                    int oldest_packet_idx = idx - inflight_packet_bytes.size();
                    const std::vector<char>& retrans_packet = packets[oldest_packet_idx];
                    send_packet(sockfd, server_addr, retrans_packet);
                    print_log_from_packet("SEND", retrans_packet, cwnd, ssthresh, false);
                }
                
            }
            // reset timeout timer, bc we have received message from server
            reset_timer(timeout_timerfd, time_out);
        }
        else if (fds[1].revents != 0) {
            // retransmission timeout, change cwnd / ssthresh, then resend the oldest packet
            timeout_arrives(cwnd, ssthresh, dup_ack_count, MSS);
            int oldest_packet_idx = idx - inflight_packet_bytes.size();
            const std::vector<char>& retrans_packet = packets[oldest_packet_idx];
            send_packet(sockfd, server_addr, retrans_packet);
            print_log_from_packet("SEND", retrans_packet, cwnd, ssthresh, false);
        }
        else if (fds[2].revents != 0) {
            // 10 sec timer
            release_resources();
            exit(EXIT_FAILURE); 
        }
        else if (fds[3].revents != 0) {
            // signal caught
            release_resources();
            exit(0); //TODO check exit code
        }
        // re-arrange inflight queue
        rearrange_queue(inflight_packet_bytes, bytes_inflight, idx, cwnd);
    } 
}

void Client::close_connection(std::vector<char>& in_packet, Header& in_header, 
        std::vector<char>& out_packet, Header& out_header, int& seq_number) {
    write_fin_packet(out_packet, out_header, seq_number);
    int expect_ack = (out_header.seq_number + 1) % max_seq_number;
    reset_timer(timeout_timerfd, time_out);
    for (;;) {
        // send FIN packet
        send_packet(sockfd, server_addr, out_packet);
        print_log("SEND", out_header, cwnd, ssthresh, false);
        reset_timer(retrans_timerfd, RTO);
        int val = poll(fds, 4, -1);
        if (val < 0) {
            // an error occurs
            print_sys_error("Bad poll calling");
            release_resources();
            exit(EXIT_FAILURE);
        }
        else if (fds[0].revents != 0) {
            recv_packet(sockfd, server_addr, in_packet, in_header, max_packet_size);
            print_log("RECV", in_header, cwnd, ssthresh, false);
            if (in_header.ack && in_header.fin && in_header.ack_number == expect_ack) {
                // received a FIN-ACK packet, respond with ACK
                int ack_number = (in_header.seq_number + 1) % max_seq_number;
                write_fin_ack_packet(out_packet, out_header, seq_number, ack_number);
                send_packet(sockfd, server_addr, out_packet);
                print_log("SEND", out_header, cwnd, ssthresh, false);
                break;
            }
            // ignore this packet
            // reset timeout timer, bc we received a message
            reset_timer(timeout_timerfd, time_out);
        }
        else if (fds[1].revents != 0) {
            // retransmission timeout, resend FIN packet
            continue;
        }
        else if (fds[2].revents != 0) {
            // timeout (>10 sec)
            FATAL("Timeout (>10 sec), exiting...\n");
            release_resources();
            exit(EXIT_FAILURE);
        }
        else if (fds[3].revents != 0) {
            // signal caught
            DEBUG("received termination signal, exiting...\n");
            release_resources();
            exit(EXIT_FAILURE);
        }
    }
    // respond to all FIN-ACK packets for 2 seconds
    struct itimerspec time_for_fin;
    struct timespec two_sec;
    two_sec.tv_sec = 2;
    two_sec.tv_nsec = 0;
    memset(&time_for_fin, 0, sizeof(time_for_fin));
    time_for_fin.it_value = two_sec;
    // temprarily reuse retransmission timer for waiting
    reset_timer(retrans_timerfd, time_for_fin);
    for (;;) {
        int val = poll(fds, 2, -1);
        if (val < 0) {
            // an error occurs
            print_sys_error("Bad poll calling");
            release_resources();
            exit(EXIT_FAILURE);
        }
        else if (fds[0].revents != 0) {
            recv_packet(sockfd, server_addr, in_packet, in_header, max_packet_size);
            print_log("RECV", in_header, cwnd, ssthresh, false);
            if (in_header.fin && in_header.ack) {
                // answer FIN-ACK packet
                int ack_number = (in_header.seq_number + 1) % max_seq_number;
                write_fin_ack_packet(out_packet, out_header, seq_number, ack_number);
                send_packet(sockfd, server_addr, out_packet);
                print_log("SEND", out_header, cwnd, ssthresh, false);
            }
            // otherwise, not a fin packet, which will be ignored
        }
        else if (fds[1].revents != 0) {
            // 2 seconds timeout, exit the for loop
            break;
        }
    }
    release_resources();
}


// send message to server
void Client::send_message(const std::vector<char>& message) {
    // initialize a random sequence number
    int seq_number = rand() % max_seq_number;
    int expect_ack = (seq_number + 1) % max_seq_number;
    std::vector<char> in_packet, out_packet;
    Header in_header, out_header;
    
    // hand-shaking period
    write_syn_packet(out_packet, out_header, seq_number);
    hand_shaking(out_packet, in_packet, in_header, expect_ack);
    int ack_number = (in_header.seq_number + 1) % max_seq_number;
    seq_number = expect_ack;
    
    // get all out-bounding packets
    std::vector<std::vector<char> > data_packets;
    std::vector<Header> data_headers;
    write_data_packets(message, data_packets, data_headers, seq_number, ack_number);
    int last_unacked_seq = data_headers[0].seq_number;
    
    // extract sequence number and calculate next ack number
    send_packets_in_window(last_unacked_seq, data_packets, in_packet, in_header); 

    // send FIN -- FIN|ACK -- end
    close_connection(in_packet, in_header, out_packet, out_header, seq_number);
}

