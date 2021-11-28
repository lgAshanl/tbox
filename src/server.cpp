#include "tbserver.h"
#include "iostream"

int main(int argc, char* argv[])
{
    TB::NetServer server;
    server.init();
    server.start();

    std::string line;
    while (true)
    {
        std::cin >> line;
        if ("stop" == line)
        {
            break;
        }
    }

    server.stop();

    return 0;
}