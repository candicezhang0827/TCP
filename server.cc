#include "server.h"
#include "packet.h"
#include "utils.h"
// C headers
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
// C++ headers
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
// LINUX headers
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signalfd.h>


Server::Server(int port, int max_packet_size, int max_seq_number) : port(port), 
    max_packet_size(max_packet_size), max_seq_number(max_seq_number) {
    // initialize UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        print_sys_error("Unable to initialize UDP socket");
        exit(EXIT_FAILURE);
    }
    
    // address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // bind address
    if (bind(sockfd, (const struct sockaddr *)&server_addr, 
                sizeof(server_addr)) < 0) {
        print_sys_error("Unable to bind address");
        exit(EXIT_FAILURE);
    }

    // create timer file descriptor, nonblock
    retrans_timerfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
    timeout_timerfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
    struct timespec TTL, TO, zero;
    TTL.tv_sec = 0;
    TTL.tv_nsec = 500000000; // 0.5 sec = 500 ms = 500000 us = 500000000 
    TO.tv_sec = 10;
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
    if (sigfd == -1) {
        print_sys_error("Unable to create signal fd");
        exit(EXIT_FAILURE);
    }  
    
    // add sockfd, timerfd, sigfd to monitor
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;
    fds[1].fd = sigfd;
    fds[1].events = POLLIN;
    fds[2].fd = retrans_timerfd;
    fds[2].events = POLLIN;
    fds[3].fd = timeout_timerfd;
    fds[3].events = POLLIN;
    
    // set random seed
    srand(time(0));
}


// write all packets to file, maintaining the relative order
// but there might be gaps between them (due to packet loss)
int Server::write_buffer_to_file(const Buffer& buffer) {
    std::string filename = std::to_string(client_id) + ".file";
    std::vector<char> content;
    for (const auto& p : buffer) {
        auto it = p.second.begin() + sizeof(Header);
        std::copy(it, p.second.end(), std::back_inserter(content));
    }
    FILE *fp = fopen(filename.c_str(), "wb+");
    if (fp == NULL) {
        print_sys_error("Cannot open file to write");
        return -1; 
    }
    fwrite(content.data(), sizeof(char), content.size(), fp);
    fclose(fp);
    return 0;
}

void Server::release_resources() {
    close(sockfd);
    close(sigfd);
    close(retrans_timerfd);
    close(timeout_timerfd);
}


void Server::write_interrupt_to_file() {
    std::string filename = std::to_string(client_id) + ".file";
    FILE* file = fopen(filename.c_str(), "wb+");
    const char* s = "INTERRUPT";
    fwrite(s, sizeof(char), strlen(s), file);
    fclose(file);
}

void Server::catch_signal() {
    struct signalfd_siginfo fdsi;
    int s = read(sigfd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
        print_sys_error("Unable to read signalfd");
        exit(EXIT_FAILURE);
    }
    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGQUIT || fdsi.ssi_signo == SIGTERM) {
        // caught termination signal
        FATAL("caught termination signal, exiting...\n")
        // close socket
        release_resources();
        // write INTERRUPT to file
        write_interrupt_to_file();
        exit(EXIT_SUCCESS);
    }
    else {
        // unknown signal, ignore?
        ERR("Caught unknown signal, ignore\n");
    }
}

void Server::write_syn_ack_packet(std::vector<char>& packet, Header& header, int& seq_number, 
        int ack_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.ack_number = ack_number;
    header.syn = true;
    header.ack = true;
    memcpy(packet.data(), &header, sizeof(header));
    seq_number = (seq_number + 1) % max_seq_number;
}

void Server::write_ack_packet(std::vector<char>& packet, Header& header, int seq_number, 
        int ack_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.ack_number = ack_number;
    header.ack = true;
    memcpy(packet.data(), &header, sizeof(header));
    // do not add 1 to seq_number
}

void Server::write_fin_ack_packet(std::vector<char>& packet, Header& header, int& seq_number, 
        int ack_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.ack_number = ack_number;
    header.ack = true;
    header.fin = true;
    memcpy(packet.data(), &header, sizeof(header));
    seq_number = (seq_number + 1) % max_seq_number;
}

/*
void Server::write_fin_packet(std::vector<char>& packet, Header& header, int& seq_number) {
    memset(&header, 0, sizeof(header));
    packet.resize(sizeof(header));
    header.seq_number = seq_number;
    header.fin = true;
    memcpy(packet.data(), &header, sizeof(header));
    seq_number = (seq_number + 1) % max_seq_number;
}
*/

void Server::hand_shaking(struct sockaddr_in& client_addr, std::vector<char>& ack_packet, 
        Header& ack_header) {
    std::vector<char> in_packet;
    Header in_header;
    for (;;) {
        int val = poll(fds, 2, -1);
        if (val < 0) {
            // error occurs
            print_sys_error("Bad poll calling");  
        }
        else {
            if (fds[0].revents != 0) {
                // expect SYN packet
                recv_packet(sockfd, client_addr, in_packet, in_header, max_packet_size);
                if (!in_header.syn) {
                    // not a SYN packet, ignore
                    fprintf(stderr, "ERR: Not a SYN packet, which will be ignored\n");
                    continue;
                }
                print_log("RECV", in_header, 0, 0, false);
                int ack_number = (in_header.seq_number + 1) % max_seq_number;
                ack_header.ack_number = ack_number;
                memcpy(ack_packet.data(), &ack_header, sizeof(ack_header));
                if (send_packet(sockfd, client_addr, ack_packet) == -1) {
                    print_sys_error("Unable to send SYN+ACK packet");
                    continue; // shouldn't exit, because client may be just disconnected
                }
                print_log("SEND", ack_header, 0, 0, false);
                // success, ready to receive data
                break;
            }
            else if (fds[1].revents != 0) {
                // signal captured
                catch_signal();
            }
        }
    }
}

void Server::insert_packet_to_buffer(Buffer& buffer, BuffIter& inorder_iter, 
        const std::vector<char>& in_packet, const Header& in_header) {
    // it is possible that inorder_iter is the end, if so, just insert to the end
    BuffIter iter = inorder_iter;
    int target_seq_number = in_header.seq_number;
    while (iter != buffer.end()) {
        printf("Isn't the last iter, moving\n");
        // shift iter until meet a bigger element
        int current_seq_number = iter->first.seq_number;
        if (current_seq_number < target_seq_number - max_seq_number / 2) {
            current_seq_number += max_seq_number;
        }
        if (target_seq_number < current_seq_number - max_seq_number / 2) {
            target_seq_number += max_seq_number;
        }
        if (current_seq_number < target_seq_number) {
            // keep moving
            printf("Keep moving\n");
            ++iter;
        }
        else if (current_seq_number == target_seq_number) {
            // duplicated out-of-order packet, do nothing
            return;
        }
        else { // current_seq_number > target_seq_number
            // good
            break;
        }
    }
    // insert new packet before iter
    BuffIter temp_iter = buffer.insert(iter, std::make_pair(in_header, in_packet));
    //if (inorder_iter == buffer.end())
    if (inorder_iter == iter)
        inorder_iter = temp_iter;
    printf("[OOO-PACKET] insert packet %lu, SEQ: %d\n", buffer.size(), in_header.seq_number);
    print_buffer(buffer);
}

void Server::move_iter_forward(Buffer& buffer, BuffIter& inorder_iter, int& ack_number) {
    BuffIter next_iter = std::next(inorder_iter);
    for (;;) {
        if (next_iter == buffer.end()) {
            printf("next_iter == buffer.end()\n");
            break;
        }
        // does next_iter is tightly connected with inorder_iter?
        int inorder_seq_number = inorder_iter->first.seq_number;
        int payload = inorder_iter->second.size() - sizeof(Header);
        int next_seq_number = next_iter->first.seq_number;
        printf("May move iter: inorder_seq: %d + payload: %d =? next_seq: %d\n", inorder_seq_number, payload, next_seq_number);
        if (((inorder_seq_number + payload) % max_seq_number) == next_seq_number) {
            ++inorder_iter;
            ++next_iter;
        }
        else {
            break;
        }
    }
    // ack number is just seq_number of last in-order packet add payload
    int payload = inorder_iter->second.size() - sizeof(Header);
    ack_number = (inorder_iter->first.seq_number + payload) % max_seq_number;
    // move inorder_iter forward
    ++inorder_iter;
}

int Server::recv_data_to_buffer(struct sockaddr_in& client_addr, std::vector<char>& in_packet, 
        Header& in_header, std::vector<char>& out_packet, Header& out_header, 
        std::list<DataPacket>& buffer, int seq_number, int expect_seq_number) {
    // pointer to last in-order packet
    auto inorder_iter = buffer.begin();
    // reset timeout timer
    reset_timer(timeout_timerfd, time_out);
    for (;;) {
        // reset retransmission timer before polling
        reset_timer(retrans_timerfd, RTO); 
        int val = poll(fds, 4, -1);
        if (val < 0) {
            print_sys_error("Bad poll calling");
            exit(EXIT_FAILURE);
        }
        if (fds[0].revents != 0) {
            recv_packet(sockfd, client_addr, in_packet, in_header, max_packet_size);
            print_log("RECV", in_header, 0, 0, false);
            // expect an ACK or FIN packet
            if (in_header.ack) {

                if (in_header.seq_number == expect_seq_number) {
                    // in order packet, insert after inorder_iter
                    inorder_iter = buffer.insert(inorder_iter, std::make_pair(in_header, in_packet));
                    printf("[INORDER-PACK] insert packet: %lu, SEQ: %d\n", buffer.size(), 
                            in_header.seq_number);
                    print_buffer(buffer);
                    // move iterator forward, possibly connect all out-of-order packets
                    int ack_number; // for reference out
                    move_iter_forward(buffer, inorder_iter, ack_number);
                    // build an cumulative ACK packet and reply
                    write_ack_packet(out_packet, out_header, seq_number, ack_number);
                    send_packet(sockfd, client_addr, out_packet);
                    print_log("SEND", out_header, 0, 0, false);
                    // update next expected in-order seq_number
                    expect_seq_number = ack_number;
                    printf("[INORDER-PACK] next_expected_seq: %d\n", expect_seq_number);
                } 
                else {
                    int in_seq_number = in_header.seq_number;
                    if (in_seq_number < expect_seq_number - max_seq_number / 2) {
                        in_seq_number += max_seq_number;
                    }
                    else if (expect_seq_number < in_seq_number - max_seq_number / 2) {
                        in_seq_number -= max_seq_number;
                    }
                    if (in_seq_number > expect_seq_number) {
                        // detect packet loss, insert this packet with linear search
                        insert_packet_to_buffer(buffer, inorder_iter, in_packet, in_header);
                    }
                    // write a duplicated-ack with lastest ack packet
                    send_packet(sockfd, client_addr, out_packet);
                    // this is a duplicated-ack, so add [DUP] at the log
                    print_log("SEND", out_header, 0, 0, true);
                }
            }
            else if (in_header.fin) {
                // close connection
                break;
            }
            else {
                fprintf(stderr, "ERR: not a ACK or FIN packet\n");
            }
            // reset timeout timer
            reset_timer(timeout_timerfd, time_out); 
        }
        else if (fds[1].revents != 0) {
            fprintf(stderr, "WARN: caught an interruption signal, exiting...\n");
            // catch signal and exit
            catch_signal();
        }
        else if (fds[2].revents != 0) {
            // retransmission timeout, resend latest out_packet
            send_packet(sockfd, client_addr, out_packet);
            print_log("SEND", out_header, 0, 0, true);
        }
        else if (fds[3].revents != 0) {
            // timeout, exit from this connection
            fprintf(stderr, "ERR: connection timeout, disconnect...\n");
            return -1; 
        }
    }
    return 0; 
}

void Server::close_connection(struct sockaddr_in& client_addr, std::vector<char>& in_packet, 
            Header& in_header, std::vector<char>& out_packet, Header& out_header, int seq_number) {
    // in_header stores FIN packet
    int ack_number = (in_header.seq_number + 1) % max_seq_number;
    write_fin_ack_packet(out_packet, out_header, seq_number, ack_number);
    // send FIN-ACK packet
    send_packet(sockfd, client_addr, out_packet);
    print_log("SEND", out_header, 0, 0, false);
    //write_fin_packet(out_packet, out_header, seq_number);
    // send FIN packet
    //send_packet(sockfd, client_addr, out_packet);
    //print_log("SEND", out_header, 0, 0, false);
    int expect_ack_number = seq_number; //(seq_number + 1) % max_seq_number;
    reset_timer(timeout_timerfd, time_out);
    for (;;) {
        reset_timer(retrans_timerfd, RTO);
        int val = poll(fds, 4, -1);
        if (val < 0) {
            print_sys_error("Bad poll calling");
            exit(EXIT_FAILURE);
        }
        if (fds[0].revents != 0) {
            recv_packet(sockfd, client_addr, in_packet, in_header, max_packet_size);
            print_log("RECV", in_header, 0, 0, false);
            if (in_header.ack && in_header.ack_number == expect_ack_number) {
                break;
            }
            reset_timer(timeout_timerfd, time_out);
        }
        else if (fds[1].revents != 0) {
            // catch signal and exit
            catch_signal();
        }
        else if (fds[2].revents != 0) {
            // retransmission timeout
            send_packet(sockfd, client_addr, out_packet);
            print_log_from_packet("SEND", out_packet, 0, 0, false);
        }
        else if (fds[3].revents != 0) {
            // timeout, force close
            break;
        }
    }
}

void Server::listen() {
    // in & out packets
    std::vector<char> in_packet, out_packet;
    Header in_header, out_header;
    //std::vector<char> packet_buffer(max_packet_size);
    //Header header_buffer;
    // event loop, one iteration for each client
    for (client_id = 1;; ++client_id) {
        // store data in a doubly-linked list
        std::list<DataPacket> data_buffer;
        // client address information
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        
        /*
         * Listen to any client
         */
        for (;;) {
            // wait only on the sockfd and signal
            int val = poll(fds, 2, -1);
            if (val < 0) {
                print_sys_error("Bad poll calling");
                exit(EXIT_FAILURE);
            }
            if (fds[0].revents != 0) {
                // check SYN packet
                recv_packet(sockfd, client_addr, in_packet, in_header, max_packet_size);
                print_log("RECV", in_header, 0, 0, false);
                if (in_header.syn) {
                    break;
                }
            }
            else if (fds[1].revents != 0){
                // received signal to quit the program
                catch_signal();
            }
        }
         
        /*
         * Hand shaking stage
         */
        int seq_number = rand() % max_seq_number;
        int ack_number = (in_header.seq_number + 1) % max_seq_number;
        write_syn_ack_packet(out_packet, out_header, seq_number, ack_number);
        // respond with a SYN-ACK packet
        send_packet(sockfd, client_addr, out_packet);
        print_log("SEND", out_header, 0, 0, false);
        //int first_seq_number = out_header.ack_number;
        
        /*
         * Receive data packets
         */
        int expect_seq_number = ack_number;
        int status = recv_data_to_buffer(client_addr, in_packet, in_header, out_packet, 
                out_header, data_buffer, seq_number, expect_seq_number);
        if (status != -1) {
            /*
             * FIN-ACK stage
             */
            close_connection(client_addr, in_packet, in_header, out_packet, out_header, seq_number);
        }

        /*
         * Write data buffer to file
         */
        write_buffer_to_file(data_buffer);
    }
}

