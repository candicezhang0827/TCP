// project headers
#include "utils.h"
#include "server.h"

// C++ headers
#include <cstdlib>
#include <cstdio>
// LINUX headers

int main(int argc, char** argv) {
    // parse arguments
    if (argc != 2) {
        FATAL("invalid number of parameters,\nshould be `./server <PORT>`\n");
        exit(EXIT_FAILURE);
    }

    int port = std::atoi(argv[1]);

    // initialize server
    int max_packet_size = 524;
    int max_seq_number = 25600;
    Server server(port, max_packet_size, max_seq_number);
    server.listen();

    return 0;
}
