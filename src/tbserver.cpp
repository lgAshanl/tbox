#include "tbserver.h"

namespace TB
{
    //////////////////////////////////////////////////////////////

    const std::string NetServer::s_mainDir(File::getHomeDir() + "/servertb");

    //---------------------------------------------------------//

    bool NetServer::init()
    {
        if (!m_queue.init())
        {
            printf("init queue err %d", errno);
            return false;
        }
        
        //if (!m_queue.bind("127.0.0.1", 30666))
        if (!m_queue.bind("0.0.0.0", 30666))
        {
            printf("bind queue err %d", errno);
            return false;
        }

        return true;
    }

    //---------------------------------------------------------//

    void NetServer::start()
    {
        File::createdir(s_mainDir);
        if (!m_queue.listen())
        {
            printf("init queue err %d", errno);
            return;
        }

        m_queue.start(true);
    }

    //---------------------------------------------------------//

    void NetServer::stop()
    {
        m_queue.stop();
    }

    //---------------------------------------------------------//

    void NetServer::stepCallback(NetOpHeader* packet, NetConnection* conn)
    {
        switch (packet->m_type)
        {
        case NetRequestType::Register:
            ProcessRegister(packet, conn);
            break;

        case NetRequestType::Login:
            ProcessLogin(packet, conn);
            break;

        case NetRequestType::FileList:
            assert(false);
            break;

        case NetRequestType::Reject:
            assert(false);
            break;

        case NetRequestType::WriteFile:
            ProcessWrite(packet, conn);
            break;

        case NetRequestType::ReadFile:
            ProcessRead(packet, conn);
            break;

        case NetRequestType::RemoveFile:
            ProcessRemove(packet, conn);
            break;

        default:
            assert(false);

            break;
        }

        delete[] packet;
    }

    //---------------------------------------------------------//

    // next packet for read
    void NetServer::sendCallback(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename(((char*)packet) + sizeof(NetOpHeader));
        std::string fullpath = NetServer::s_mainDir + "/" + filename;

        auto iter = m_files.find(fullpath);
        assert(m_files.end() != iter);

        FileInProgress* file = iter->second;

        uint8_t* reply = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)reply;

        header->m_type = NetRequestType::ReadFile;
        memcpy(reply + sizeof(NetOpHeader), filename.c_str(), filename.size() + 1);

        const bufsize_t offset = sizeof(NetOpHeader) + filename.size() + 1;
        bufsize_t segment_size = NetConnection::s_packetSize - offset;
        file->ReadNextSegment(reply + offset, segment_size);

        const size_t packet_size = offset + segment_size;
        header->m_size = packet_size;

        if (0 != segment_size)
            m_queue.sendRequest(NetRequest(header, NetServer::staticSendCallback, this), conn);
        else
        {
            m_queue.sendRequest(NetRequest(header, nullptr, nullptr), conn);
            m_files.erase(iter);
            delete file;
        }
    }

    //---------------------------------------------------------//

    void NetServer::ProcessRegister(NetOpHeader* packet, NetConnection* conn)
    {
        std::string login(((char*)packet) + sizeof(NetOpHeader));
        std::string passw(((char*)packet) + sizeof(NetOpHeader) + login.size() + 1);

        const std::string userdir = NetServer::s_mainDir + "/" + login;
        const bool is_old_user = File::checkDirExistance(userdir);

        uint8_t* reply = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)reply;

        if (is_old_user)
        {
            header->m_type = NetRequestType::Reject;
            header->m_size = sizeof(NetOpHeader);
        }
        else
        {
            File::createdir(userdir);
            const std::string real_passw_file = NetServer::s_mainDir + "/" + login + "/.passw";
            File::writeBuf(real_passw_file, passw.c_str(), passw.size() + 1);

            header->m_type = NetRequestType::FileList;
            header->m_size = sizeof(NetOpHeader);
        }

        m_queue.sendRequest(NetRequest(header, nullptr, nullptr), conn);
    }

    //---------------------------------------------------------//

    void NetServer::ProcessLogin(NetOpHeader* packet, NetConnection* conn)
    {
        std::string login(((char*)packet) + sizeof(NetOpHeader));
        std::string passw(((char*)packet) + sizeof(NetOpHeader) + login.size() + 1);

        const std::string userdir = NetServer::s_mainDir + "/" + login;
        const bool is_old_user = File::checkDirExistance(userdir);
        const bool is_passwd_valid = checkPassw(login, passw);

        uint8_t* reply = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)reply;

        if (is_old_user && is_passwd_valid)
        {
            std::list<std::string> file_list = File::getFileList(NetServer::s_mainDir, login);
            header->m_type = NetRequestType::FileList;

            bufsize_t offset = sizeof(NetOpHeader);
            for (auto file : file_list)
            {
                const bufsize_t namesize = file.size() + 1;
                if (offset + namesize > NetConnection::s_packetSize)
                {
                    break;
                }

                memcpy(reply + offset, file.c_str(), namesize);
                offset += namesize;
            }

            header->m_size = offset;
        }
        else
        {
            header->m_type = NetRequestType::Reject;
            header->m_size = sizeof(NetOpHeader);
        }

        m_queue.sendRequest(NetRequest(header, nullptr, nullptr), conn);
    }

    //---------------------------------------------------------//

    void NetServer::ProcessWrite(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename(((char*)packet) + sizeof(NetOpHeader));

        std::string fullpath = NetServer::s_mainDir + "/" + filename;

        const bufsize_t offset = sizeof(NetOpHeader) + filename.size() + 1;

        auto iter = m_files.find(fullpath);

        if (m_files.end() == iter)
        {
            FileInProgress* file = new FileInProgress(fullpath);
            if (!file->create(0))
                throw "create";

            m_files.emplace(fullpath, file);

            file->WriteNextSegment((uint8_t*)packet + offset, packet->m_size - offset);
        }
        else
        {
            FileInProgress* file = iter->second;

            if (packet->m_size == offset)
            {
                m_files.erase(iter);
                delete file;
            }
            else
            {
                file->WriteNextSegment((uint8_t*)packet + offset, packet->m_size - offset);
            }
        }
    }

    //---------------------------------------------------------//

    void NetServer::ProcessRead(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename(((char*)packet) + sizeof(NetOpHeader));
        std::string fullpath = NetServer::s_mainDir + "/" + filename;

        auto iter = m_files.find(fullpath);
        assert(m_files.end() == iter);

        FileInProgress* file = new FileInProgress(fullpath);
        if (!file->open())
            throw "open";

        m_files.emplace(fullpath, file);

        uint8_t* reply = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)reply;

        header->m_type = NetRequestType::ReadFile;
        memcpy(reply + sizeof(NetOpHeader), filename.c_str(), filename.size() + 1);

        const bufsize_t offset = sizeof(NetOpHeader) + filename.size() + 1;
        bufsize_t segment_size = NetConnection::s_packetSize - offset;
        file->ReadNextSegment(reply + offset, segment_size);

        header->m_size = offset + segment_size;

        m_queue.sendRequest(
            NetRequest(header, &NetServer::staticSendCallback, this), conn);
    }

    //---------------------------------------------------------//

    void NetServer::ProcessRemove(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename(((char*)packet) + sizeof(NetOpHeader));
        std::string fullpath = NetServer::s_mainDir + "/" + filename;

        File::removeFile(fullpath);
    }

    //---------------------------------------------------------//

    bool NetServer::checkPassw(std::string& login, std::string& passw)
    {
        const std::string real_passw_file = NetServer::s_mainDir + "/" + login + "/.passw";
        bufsize_t real_passw_size = 256;
        uint8_t real_passw[256] = { 0 };

        File::readToBuf(real_passw_file, (uint8_t*)&real_passw, real_passw_size);
        if ((real_passw_size != (passw.size() + 1)) ||
            (0 != memcmp(passw.c_str(), (uint8_t*)&real_passw, real_passw_size)))
        {
            return false;
        }

        return true;
    }

}