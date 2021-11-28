#include <cstring>
#include <map>

#include <gtk/gtk.h>

#include "tbfile.h"
#include "tbnetqueue.h"

#pragma once

namespace TB
{
    class ClientApp
    {
    public:
        ClientApp()
          : m_app(nullptr),
            m_provider(nullptr),
            m_files(),
            m_queue(this, ClientApp::staticStepCallback),
            m_serverConnection(nullptr),
            m_nextFileId(0)
        {
        }

        ClientApp(const ClientApp& other) = delete;
        ClientApp(ClientApp&& other) noexcept = delete;
        ClientApp& operator=(const ClientApp& other) = delete;
        ClientApp& operator=(ClientApp&& other) noexcept = delete;

        bool init();

        int start(int argc, char* argv[]);

        void deinit();

    private:

        struct LoginCallbckData
        {
            ClientApp* m_app;
            GtkWidget* m_loginWidget;
            GtkWidget* m_passwWidget;
        };

    private:

        void netRegister(const std::string& login, const std::string& passw);
    
        void netLogin(const std::string& login, const std::string& passw);

        void netReadFile(std::string& filepath);

        void netWriteFile(std::string& filepath);

        void netRemoveFile(std::string& filepath);

        void sendCallback(NetOpHeader* request, NetConnection* conn);

        void stepCallback(NetOpHeader* request, NetConnection* conn);

        void ProcessGetFileList(NetOpHeader* packet, NetConnection* conn);

        void ProcessRead(NetOpHeader* packet, NetConnection* conn);

        void ProcessReject(NetOpHeader* packet, NetConnection* conn); 

    private:
        GtkWidget* getLoginWindow();

        GtkWidget* getAcessDeniedWindow();

        GtkWidget* getMainWindow();

        void refreshMainWindow();

    private:

        static inline void staticStepCallback(void* object,
                                              NetOpHeader* package,
                                              NetConnection* conn);

        static inline void staticSendCallback(void* object,
                                              NetOpHeader* package,
                                              NetConnection* conn);

    private:

        void enableCSS();

        void buttonEnableCSS(GtkWidget* button);

        static void activate(GtkApplication* app, gpointer user_data);

        static void registerButtonCallback(GtkWidget* button, gpointer window);

        static void loginButtonCallback(GtkWidget* button, gpointer window);

        static void fileButtonCallback(GtkWidget* button, gpointer window);

        static void fileRemoveButtonCallback(GtkWidget* button, gpointer window);

        static void on_drag_data_received(
            GtkWidget *wgt, GdkDragContext *context, gint x, gint y,
            GtkSelectionData *seldata, guint info, guint time, gpointer data);

    public:
        GtkApplication* m_app;

        GtkCssProvider* m_provider;

        GtkWidget* m_window;

        std::map<std::string, FileInProgress*> m_files;

        NetQueue m_queue;

        NetConnection* m_serverConnection;

        std::atomic<uint64_t> m_nextFileId;

        std::mutex m_mtx_file_list;

        std::list<std::string> m_file_list;

        std::string m_login;

        TBEvent m_loginEvent;

        TBEvent m_readEvent;

        TBEvent m_writeEvent;

        std::mutex m_renderMtx;

    public:

        static const std::string s_mainDir;

        static GtkTargetEntry targetentries[];
    };

    void ClientApp::staticStepCallback(void* object, NetOpHeader* package,
                                       NetConnection* conn)
    {
        reinterpret_cast<ClientApp*>(object)->stepCallback(package, conn);
    }

    void ClientApp::staticSendCallback(void* object, NetOpHeader* package,
                                       NetConnection* conn)
    {
        reinterpret_cast<ClientApp*>(object)->sendCallback(package, conn);
    }

} // namespace TB