/*
 * UdpGroupDest_test.cpp
 *
 * @build   make evpp
 * @client  bin/UdpClient_test 9997 230.1.1.25
 * @server  bin/UdpGroupDest_test 9997 230.1.1.25
 *
 */

#include <iostream>

#include "UdpGroup.h"

using namespace hv;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s port\n", argv[0]);
        return -10;
    }
    int port = atoi(argv[1]);
    const char* group = "230.1.1.25";
    if (argc > 2) {
        group = argv[2];
    }

    UdpGroup ug;
    int bindfd = ug.createsocket(port);
    if (bindfd < 0) {
        return -20;
    }
	int rst = ug.joinGroup(group);
    printf("udp group bind on port %d, bindfd=%d rst=%d ...\n", port, bindfd, rst);
    ug.onMessage = [](const SocketChannelPtr& channel, Buffer* buf) {
        // echo
        printf(" >>> %.*s\n", (int)buf->size(), (char*)buf->data());
    };
    ug.start();

    std::string str;
    while (std::getline(std::cin, str)) {
        if (str == "close") {
            ug.closesocket();
            break;
        } else if (str == "leave") {
            int c = ug.leaveGroup(group);
			printf("leave group rst=%d\n", c);
        } else if (str == "join") {
            int c = ug.joinGroup(group);
			printf("join group rst=%d\n", c);
        } else {
			break;
        }
    }

    return 0;
}
