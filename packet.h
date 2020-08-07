#ifndef _PACKET_H_
#define _PACKET_H_

#include <cstdio>
#include <vector>
#include <list>

struct Header {
    unsigned short seq_number; // 2
    unsigned short ack_number; // 2
    bool ack;                  // 1
    bool syn;                  // 1
    bool fin;                  // 1
    char padding[5];           // 1
}; // total: 12 bytes

typedef std::pair<Header, std::vector<char> > DataPacket;
typedef std::list<DataPacket> Buffer;
typedef Buffer::iterator BuffIter;

#endif
