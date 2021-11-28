#include <map>
#include <string>
#include <queue>
#include <list>
#include <atomic>
#include <assert.h>
#include <thread>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/sockios.h>
#include <fcntl.h>

#include "types.h"
#include "tbring.h"

#pragma once

namespace TB
{
    enum NetRequestType
    {
        Register = 1,
        Login = 2,
        FileList = 3,
        Reject = 4,
        WriteFile = 5,
        ReadFile = 6,
        RemoveFile = 7
    };

    struct NetOpHeader
    {
        uint64_t m_id;

        NetRequestType m_type;

        bufsize_t m_size;
    };

    // FD:
    class NetConnection;

    class NetAddress
    {
    public:

        explicit NetAddress(sockaddr_in sock)
          : m_addr(sock)
        { };

        static bool getSockAddr(const char* addr, uint16_t port, sockaddr_in& netaddr);

    public:

        const struct sockaddr_in m_addr;
    };

    class NetRequest
    {
    public:

        typedef void (*SendCallback)(void* object, NetOpHeader* packet, NetConnection* conn);

        NetRequest(NetOpHeader* packet,
                   SendCallback sendCallback, void* callbackObject)
          : m_packet(packet),
            m_sendCallback(sendCallback),
            m_callbackObject(callbackObject)
        { };

    public:

        //const bufsize_t m_size;

        NetOpHeader* const m_packet;

        SendCallback m_sendCallback;

        void* const m_callbackObject;
    };

    class NetConnection
    {
        friend class NetQueue;
    public:
        NetConnection(sockaddr_in addr, handle_t socket)
          : m_addr(addr),
            m_socket(socket),
            m_buf(new uint8_t[s_packetSize]),
            m_size(0)
        { };

        ~NetConnection()
        {
            delete[] m_buf;
        }

    private:

        bool nextPacket(void* buf);

        void sendPackets();

        bufsize_t sockSendSize();

        void setSockRXBuf(bufsize_t size);

        void setSockTXBuf(bufsize_t size);

    public:

        static constexpr bufsize_t s_NetHeaderSize = sizeof(NetOpHeader);
    
        static constexpr bufsize_t s_packetSize = 200000;

        static constexpr bufsize_t s_sockSize = 2 << 17;

    public:

        const NetAddress m_addr;

        const handle_t m_socket;

    private:

        MTRingPtr<NetRequest> m_sendQueue;

        uint8_t* m_buf;

        bufsize_t m_size;
    };

    class NetQueue
    {
    public:

        typedef void (*StepCallback)(void* object, NetOpHeader* package, NetConnection* conn);

        NetQueue() = delete;

        NetQueue(void* object, StepCallback callback);

        bool init();

        bool bind(const char* addr, uint16_t port);

        NetConnection* connect(const char* addr, uint16_t port);

        bool listen();

        void start(bool init_listen = false);

        void stop();

        void sendRequest(NetRequest request, NetConnection* conn);

    private:  

        void accept();

        void work();

        void step();

           

    private:

        handle_t m_socket;

        MTRingPtr<NetConnection> m_connections;

        void* m_callbackObject;

        StepCallback m_stepCallback;

        std::atomic<bool> m_stopFlag;

        std::thread m_listener;

        std::thread m_workthread;

    public:

        static constexpr uint32_t s_connectionsPerStep = 64;
    };

}