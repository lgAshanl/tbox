#include <cstring>
#include <map>

#include "tbfile.h"
#include "tbnetqueue.h"

#ifndef NETSERVER_H
#define NETSERVER_H

namespace TB
{

    class NetServer
    {
    public:
        NetServer()
          : m_files(),
            m_queue(this, NetServer::staticStepCallback),
            m_nextFileId(0)
        {
        }

        NetServer(const NetServer& other) = delete;
        NetServer(NetServer&& other) noexcept = delete;
        NetServer& operator=(const NetServer& other) = delete;
        NetServer& operator=(NetServer&& other) noexcept = delete;

        bool init();

        void start();

        void stop();

    private:
        void stepCallback(NetOpHeader* request, NetConnection* conn);

        void sendCallback(NetOpHeader* request, NetConnection* conn);

        void ProcessRegister(NetOpHeader* packet, NetConnection* conn);

        void ProcessLogin(NetOpHeader* packet, NetConnection* conn);

        void ProcessWrite(NetOpHeader* packet, NetConnection* conn);

        void ProcessRead(NetOpHeader* packet, NetConnection* conn);

        void ProcessRemove(NetOpHeader* packet, NetConnection* conn);

        bool checkPassw(std::string& login, std::string& passw);

    public:
        static inline void staticStepCallback(void* object,
                                              NetOpHeader* package,
                                              NetConnection* conn);

        static inline void staticSendCallback(void* object,
                                              NetOpHeader* package,
                                              NetConnection* conn);

    private:
        std::map<std::string, FileInProgress*> m_files;

        NetQueue m_queue;

        std::atomic<uint64_t> m_nextFileId;

    public:

        static const std::string s_mainDir;
    };

    void NetServer::staticStepCallback(void* object, NetOpHeader* package,
                                       NetConnection* conn)
    {
        reinterpret_cast<NetServer*>(object)->stepCallback(package, conn);
    }

    void NetServer::staticSendCallback(void* object, NetOpHeader* package,
                                       NetConnection* conn)
    {
        reinterpret_cast<NetServer*>(object)->sendCallback(package, conn);
    }

} // namespace TB

#endif