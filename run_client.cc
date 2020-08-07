// project headers
#include "utils.h"
#include "client.h"

// C++ headers
#include <cstdlib>
#include <cstdio>
// LINUX headers

int main(int argc, char** argv) {
    // parse arguments
    if (argc != 4) {
        FATAL("invalid number of parameters,\nshould be `./client <HOSTNAME-OR-IP> <PORT> <FILENAME>`\n");
        exit(EXIT_FAILURE);
    }
    std::string ip_addr = argv[1];
    int port = std::atoi(argv[2]);
    std::string file_name = argv[3];
    
    // initialize client
    int max_seq_num = 25600;
    int max_packet_size = 524;
    // controling the window size
    int cwnd = 512;
    int max_cwnd = 10240;
    int ssthresh = 5120;
    int MSS = 512;
    Client client(ip_addr, port, max_seq_num, max_packet_size, cwnd, max_cwnd, ssthresh, MSS);
    
    // send file
    client.send_file(file_name);
    return 0;
}
