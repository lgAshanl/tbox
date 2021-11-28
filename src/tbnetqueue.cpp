#include "tbnetqueue.h"

#include <cstring>
#include <unistd.h>

namespace TB
{
    //////////////////////////////////////////////////////////////
    bool NetAddress::getSockAddr(const char* addr, uint16_t port, sockaddr_in& netaddr)
    {
        if (1 != inet_pton(AF_INET, addr, &(netaddr.sin_addr)))
        {
            return false;
        }

        netaddr.sin_family = AF_INET;
        netaddr.sin_port  = htons(port);

        return true;
    }

    //////////////////////////////////////////////////////////////

    bool NetConnection::nextPacket(void* buf)
    {
        const bufsize_t readsize = s_packetSize - m_size;

        int res = read(m_socket, m_buf + m_size, readsize);
        if (-1 == res)
        {
            if (m_size < s_NetHeaderSize)
            {
                return false;
            }
            res = 0;
        }

        m_size += res;
        if (m_size < s_NetHeaderSize)
            return false;

        NetOpHeader* header = reinterpret_cast<NetOpHeader*>(m_buf);
        const bufsize_t package_size = header->m_size;
        if (package_size > m_size)
        {
            return false;
        }

        memcpy(buf, m_buf, package_size);
        memmove(m_buf, m_buf + package_size, m_size - package_size);
        m_size -= package_size;

        return true;
    }

    //---------------------------------------------------------//

    void NetConnection::sendPackets()
    {
        if (!m_sendQueue.isEmpty())
        {
            if (sockSendSize() < s_packetSize)
            {
                return;
            }

            NetRequest* request = m_sendQueue.pop();
            if (nullptr == request)
            {
                return;
            }

            const bufsize_t packet_size = ((NetOpHeader*)request->m_packet)->m_size;

            assert(packet_size <= s_packetSize);

            bufsize_t nwrite = 0;
            while (nwrite < packet_size)
            {
                int res = write(m_socket, ((uint8_t*)request->m_packet) + nwrite, packet_size - nwrite);
                if (res < 0)
                {
                    if (EAGAIN == errno)
                    {
                        res = 0;
                    }
                    else
                    {
                        printf("sockerr write: %d", errno);
                        throw "wasted";
                    }
                }

                nwrite += res;
            }

            if (nullptr != request->m_sendCallback)
            {
                request->m_sendCallback(request->m_callbackObject, request->m_packet, this);
            }
            
            delete[] request->m_packet;
            delete request;
        }
    }

    //---------------------------------------------------------//

    bufsize_t NetConnection::sockSendSize()
    {
        int option;
        socklen_t optlen = sizeof(int);
        if (0 != getsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &option, &optlen))
        {
            return 0;
        }

        assert(option >= s_packetSize);

        int count;
        if (0 != ioctl(m_socket, SIOCOUTQ, &count))
        {
            return 0;
        }

        const int32_t res = option - count;
        return (res > 0) ? ((bufsize_t)res / 2): 0; 
    }

    //---------------------------------------------------------//

    void NetConnection::setSockRXBuf(bufsize_t size)
    {
        const int option = (int)size;
        setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, (char*)(&option), sizeof(int));
    }

    //---------------------------------------------------------//

    void NetConnection::setSockTXBuf(bufsize_t size)
    {
        const int option = (int)size;
        setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (char*)(&option), sizeof(int));
    }

    //////////////////////////////////////////////////////////////

    NetQueue::NetQueue(void* object, StepCallback callback)
      : m_socket(g_badHandle),
        m_connections(),
        m_callbackObject(object),
        m_stepCallback(callback),
        m_stopFlag(false),
        m_listener(),
        m_workthread()
    { }
   
    bool NetQueue::init()
    {
        m_socket = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (g_badHandle == m_socket)
        {
            return false;
        }

        const int optval = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

        return true;
    }

    //---------------------------------------------------------//

    bool NetQueue::bind(const char* addr, uint16_t port)
    {
        sockaddr_in saddr = { 0 };
        if (!NetAddress::getSockAddr(addr, port, saddr))
        {
            return false;
        }

        if (-1 == ::bind(m_socket, (::sockaddr*)&saddr, (socklen_t)sizeof(saddr)))
        {
            printf("binderr %d", errno);
            return false;
        }

        return true;
    }

    //---------------------------------------------------------//

    NetConnection* NetQueue::connect(const char* addr, uint16_t port)
    {
        sockaddr_in saddr = { 0 };
        if (!NetAddress::getSockAddr(addr, port, saddr))
        {
            return nullptr;
        }

        while (0 != ::connect(m_socket, (::sockaddr*)&saddr, (socklen_t)sizeof(saddr)))
        {
            switch (errno)
            {
            case EINTR:
                break;

            case EINPROGRESS:
            {
                pollfd handler = {.fd = m_socket, .events = POLLOUT, .revents = 0 };
                while (true){
                    const int res = poll(&handler, 1, -1);
                    if (1 == res)
                    {
                        sockaddr checkaddr;
                        socklen_t socklen = sizeof(checkaddr);
                        if (0 != getpeername(m_socket, &checkaddr, &socklen))
                        {
                            return nullptr;
                        }

                        NetConnection* conn = new NetConnection(saddr, m_socket);
                        conn->setSockTXBuf(NetConnection::s_sockSize);
                        conn->setSockRXBuf(NetConnection::s_sockSize);

                        m_connections.push(conn);

                        return conn;
                    }
                };
                
            }
            
            default:
                return nullptr;
            }
        }

        return nullptr;
    }

    //---------------------------------------------------------//

    bool NetQueue::listen()
    {
        if (0 != ::listen(m_socket, SOMAXCONN))
        {
            return false;
        }
        return true;
    }

    //---------------------------------------------------------//

    void NetQueue::start(bool init_listen)
    {
        m_stopFlag.store(false, std::memory_order_seq_cst);

        if (init_listen)
            new (&m_listener) std::thread(&NetQueue::accept, this);

        new (&m_workthread) std::thread(&NetQueue::work, this);
    }

    //---------------------------------------------------------//

    void NetQueue::stop()
    {
        m_stopFlag.store(true, std::memory_order_seq_cst);

        if (m_listener.joinable())
            m_listener.join();

        m_workthread.join();
    }

    //---------------------------------------------------------//

    void NetQueue::sendRequest(NetRequest request, NetConnection* conn)
    {
        conn->m_sendQueue.push(new NetRequest(request));
    }

    //---------------------------------------------------------//

    void NetQueue::accept()
    {
        while (!m_stopFlag.load(std::memory_order_relaxed))
        {
            pollfd handler = { 0 };
            handler.fd = m_socket;
            handler.events = POLLIN;
            handler.revents = 0;

            const int res = poll(&handler, 1, 1);
            if (1 == res)
            {
                sockaddr_in addr = { 0 };
                socklen_t socklen = sizeof(sockaddr);
                const handle_t client_sock = ::accept(m_socket, (sockaddr*)&addr, &socklen);

                if (g_badHandle != client_sock)
                {
                    //printf("accept %d", addr.sin_addr.s_addr);

                    fcntl(client_sock, F_SETFL, O_NONBLOCK);

                    NetConnection* connect = new NetConnection(addr, client_sock);
                    connect->setSockTXBuf(NetConnection::s_sockSize);
                    connect->setSockRXBuf(NetConnection::s_sockSize);

                    m_connections.push(connect);
                }
            } 
        }
    }

    //---------------------------------------------------------//

    void NetQueue::work()
    {
        while (!m_stopFlag.load(std::memory_order_relaxed))
        {
            step();
            std::this_thread::yield();
        }
    }

    //---------------------------------------------------------//

    void NetQueue::step()
    {
        // TODO: resend

        NetConnection* connections[s_connectionsPerStep] = { 0 };
        uint32_t nconnections = 0;
        for (; nconnections < s_connectionsPerStep; ++nconnections)
        {
            NetConnection* conn = m_connections.pop();
            if (nullptr == conn)
                break;

            connections[nconnections] = conn;
        }

        pollfd handlers[s_connectionsPerStep] = { 0 };
        for (uint32_t i = 0; i < nconnections; ++i)
        {
            handlers[i].fd = connections[i]->m_socket;
            handlers[i].events = POLLIN;
            handlers[i].revents = 0;
        }

        for (uint32_t i = 0; i < nconnections; ++i)
        {
            connections[i]->sendPackets();
        }

        const int poll_res = poll(handlers, nconnections, 1);
        if (0 < poll_res)
        {
            for (uint32_t i = 0; i < nconnections; ++i)
            {
                NetConnection* const connect = connections[i];
                if (0 == (POLLIN & handlers[i].revents))
                {
                    if (0 != ((POLLERR | POLLNVAL) & handlers[i].revents))
                    {
                        printf("sockerr \n");

                        return;
                    }
                }

                uint8_t* packet = new uint8_t[NetConnection::s_packetSize];

                while (connect->nextPacket(packet))
                {
                    m_stepCallback(m_callbackObject, (NetOpHeader*)packet, connect);
                    packet = new uint8_t[NetConnection::s_packetSize];

                }
                
                delete[] packet;
            }
        }

        for (uint32_t i = 0; i < nconnections; ++i)
        {
            m_connections.push(connections[i]);
        }
    }

    //---------------------------------------------------------//

}