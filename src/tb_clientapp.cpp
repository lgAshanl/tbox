#include "tb_clientapp.h"

namespace TB
{
    //////////////////////////////////////////////////////////////

    GtkTargetEntry ClientApp::targetentries[] =
    {
        { (gchar*)"text/uri-list", 0, 0}
    };

    //---------------------------------------------------------//

    const std::string ClientApp::s_mainDir(File::getHomeDir() + "/clienttb");

    //////////////////////////////////////////////////////////////

    bool ClientApp::init()
    {
        if (!m_queue.init())
        {
            printf("CLI init queue err %d", errno);
            return false;
        }
        
        m_serverConnection = m_queue.connect("3.144.218.98", 30666);
        //m_serverConnection = m_queue.connect("127.0.0.1", 30666);
        assert(nullptr != m_serverConnection);

        m_app =
            gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
        g_signal_connect(m_app, "activate", G_CALLBACK(ClientApp::activate),
                         this);

        enableCSS();     

        return true;
    }

    //---------------------------------------------------------//

    int ClientApp::start(int argc, char* argv[])
    {
        File::createdir(ClientApp::s_mainDir);
        m_queue.start(false);
        return g_application_run(G_APPLICATION(m_app), argc, argv);
    }

    //---------------------------------------------------------//

    void ClientApp::deinit()
    {
        g_object_unref(m_app);

        m_queue.stop();
    }

    //////////////////////////////////////////////////////////////

    void ClientApp::netRegister(const std::string& login, const std::string& passw)
    {
        m_loginEvent.reinit();

        uint8_t* packet = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)packet;
        header->m_type = NetRequestType::Register;
        size_t offset = sizeof(NetOpHeader);

        memcpy(packet + offset, login.c_str(), login.size() + 1);

        offset += login.size() + 1;

        memcpy(packet + offset, passw.c_str(), passw.size() + 1);

        offset += passw.size() + 1;

        const size_t packet_size = offset;
        header->m_size = packet_size;

        m_queue.sendRequest(
            NetRequest(header, nullptr, nullptr),
            m_serverConnection);
    }

    //---------------------------------------------------------//

    void ClientApp::netLogin(const std::string& login, const std::string& passw)
    {
        m_loginEvent.reinit();

        uint8_t* packet = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)packet;
        header->m_type = NetRequestType::Login;
        size_t offset = sizeof(NetOpHeader);

        memcpy(packet + offset, login.c_str(), login.size() + 1);

        offset += login.size() + 1;

        memcpy(packet + offset, passw.c_str(), passw.size() + 1);

        offset += passw.size() + 1;

        const size_t packet_size = offset;
        header->m_size = packet_size;

        m_queue.sendRequest(
            NetRequest(header, nullptr, nullptr),
            m_serverConnection);
    }

    //---------------------------------------------------------//

    void ClientApp::netReadFile(std::string& filepath)
    {
        m_readEvent.reinit();

        uint8_t* packet = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)packet;
        header->m_type = NetRequestType::ReadFile;
        size_t offset = sizeof(NetOpHeader);

        memcpy(packet + offset, m_login.c_str(), m_login.size());
        offset += m_login.size();

        packet[offset] = '/';
        ++offset;

        memcpy(packet + offset, filepath.c_str(), filepath.size() + 1);
        offset += filepath.size() + 1;

        const size_t packet_size = offset;
        header->m_size = packet_size;

        m_queue.sendRequest(
            NetRequest(header, nullptr, nullptr), m_serverConnection);
    }

    //---------------------------------------------------------//

    void ClientApp::netWriteFile(std::string& filepath)
    {
        m_writeEvent.reinit();

        std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
        std::string fullpath = ClientApp::s_mainDir + "/" + m_login + "/" + filename;
        auto iter = m_files.find(fullpath);
        assert(m_files.end() == iter);

        FileInProgress* file = new FileInProgress(filepath);
        if (!file->open())
            throw "open";

        m_files.emplace(fullpath, file);

        uint8_t* packet = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)packet;
        header->m_type = NetRequestType::WriteFile;
        size_t offset = sizeof(NetOpHeader);

        memcpy(packet + offset, m_login.c_str(), m_login.size());
        offset += m_login.size();

        packet[offset] = '/';
        ++offset;

        memcpy(packet + offset, filename.c_str(), filename.size() + 1);
        offset += filename.size() + 1;

        bufsize_t segment_size = NetConnection::s_packetSize - offset;
        file->ReadNextSegment(packet + offset, segment_size);

        const size_t packet_size = offset + segment_size;
        header->m_size = packet_size;

        m_queue.sendRequest(
            NetRequest(header, ClientApp::staticSendCallback, this),
            m_serverConnection);
    }

    //---------------------------------------------------------//

    void ClientApp::netRemoveFile(std::string& filepath)
    {
        uint8_t* packet = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)packet;
        header->m_type = NetRequestType::RemoveFile;
        size_t offset = sizeof(NetOpHeader);

        memcpy(packet + offset, m_login.c_str(), m_login.size());
        offset += m_login.size();

        packet[offset] = '/';
        ++offset;

        memcpy(packet + offset, filepath.c_str(), filepath.size() + 1);
        offset += filepath.size() + 1;

        const size_t packet_size = offset;
        header->m_size = packet_size;

        m_queue.sendRequest(
            NetRequest(header, nullptr, nullptr), m_serverConnection);
    }

    //---------------------------------------------------------//

    // next packet for write
    void ClientApp::sendCallback(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename(((char*)packet) + sizeof(NetOpHeader));
        std::string fullpath = ClientApp::s_mainDir + "/" + filename;

        auto iter = m_files.find(fullpath);
        assert(m_files.end() != iter);

        FileInProgress* file = iter->second;

        uint8_t* reply = new uint8_t[NetConnection::s_packetSize];
        NetOpHeader* header = (NetOpHeader*)reply;

        header->m_type = NetRequestType::WriteFile;
        memcpy(reply + sizeof(NetOpHeader), filename.c_str(), filename.size() + 1);

        const bufsize_t offset = sizeof(NetOpHeader) + filename.size() + 1;
        bufsize_t segment_size = NetConnection::s_packetSize - offset;
        file->ReadNextSegment(reply + offset, segment_size);

        header->m_size = offset + segment_size;

        if (0 != segment_size)
            m_queue.sendRequest(NetRequest(header, ClientApp::staticSendCallback, this), conn);
        else
        {
            m_queue.sendRequest(NetRequest(header, nullptr, nullptr), conn);
            m_files.erase(iter);
            delete file;

            {
                std::lock_guard<std::mutex> lock(m_mtx_file_list);
                m_file_list.emplace_back(filename.substr(filename.find_last_of("/\\") + 1));
            }

            m_writeEvent.set();
        }
    }

    //---------------------------------------------------------//

    void ClientApp::stepCallback(NetOpHeader* packet, NetConnection* conn)
    {
        switch (packet->m_type)
        {
        case NetRequestType::Register:
            assert(false);
            break;

        case NetRequestType::Login:
            assert(false);
            break;

        case NetRequestType::FileList:
            ProcessGetFileList(packet, conn);
            break;

        case NetRequestType::Reject:
            ProcessReject(packet, conn);
            break;

        case NetRequestType::WriteFile:
            assert(false);
            break;

        case NetRequestType::ReadFile:
            ProcessRead(packet, conn);
            break;

        case NetRequestType::RemoveFile:
            ProcessRead(packet, conn);
            break;

        default:
            assert(false);

            break;
        }

        delete[] packet;
    }

    //---------------------------------------------------------//

    void ClientApp::ProcessGetFileList(NetOpHeader* packet, NetConnection* conn)
    {
        const bufsize_t namesize = packet->m_size - sizeof(NetOpHeader);

        //const bool is_file_list_finished = File::checkDirExistance(ClientApp::m_mainDir, login);

        bufsize_t offset = sizeof(NetOpHeader);
        while (offset < packet->m_size)
        {
            std::string filename(((char*)packet) + offset);
            offset += filename.size() + 1;

            std::lock_guard<std::mutex> lock(m_mtx_file_list);
            m_file_list.push_back(std::move(filename));
        }

        m_loginEvent.set();
    }

    //---------------------------------------------------------//

    void ClientApp::ProcessRead(NetOpHeader* packet, NetConnection* conn)
    {
        std::string filename = std::string(((char*)packet) + sizeof(NetOpHeader));
        std::string fullpath = ClientApp::s_mainDir + "/" + filename;

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
                m_readEvent.set();
            }
            else
            {
                file->WriteNextSegment((uint8_t*)packet + offset, packet->m_size - offset);
            }
        }
    }

    //---------------------------------------------------------//

    void ClientApp::ProcessReject(NetOpHeader* packet, NetConnection* conn)
    {
        m_login.clear();
        m_loginEvent.set();
    }

    //////////////////////////////////////////////////////////////

    GtkWidget* ClientApp::getLoginWindow()
    {
        assert((void*)this == (void*)(&m_app));

        GtkWidget* window;
        GtkWidget* grid;
        GtkWidget *Login_button, *Quit_button;
        GtkWidget* u_name;
        GtkWidget* pass;
        GtkWidget* label_user;
        GtkWidget* label_pass;
        GtkWidget* button_container;

        window = gtk_application_window_new(m_app);
        gtk_window_set_title(GTK_WINDOW(window), "Login page");
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        gtk_container_set_border_width(GTK_CONTAINER(window), 10);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

        grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_container_add(GTK_CONTAINER(window), grid);

        label_user = gtk_label_new("Username  ");
        label_pass = gtk_label_new("Password  ");

        u_name = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(u_name), "Username");
        gtk_grid_attach(GTK_GRID(grid), label_user, 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), u_name, 1, 1, 2, 1);

        pass = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(pass), "Password");
        gtk_grid_attach(GTK_GRID(grid), label_pass, 0, 2, 1, 1);
        gtk_entry_set_visibility(GTK_ENTRY(pass), 0);
        gtk_grid_attach(GTK_GRID(grid), pass, 1, 2, 1, 1);

        LoginCallbckData* callbackInfo =
            new LoginCallbckData{this, u_name, pass};

        Login_button = gtk_button_new_with_label("Log in");
        g_signal_connect(Login_button, "clicked",
                         G_CALLBACK(ClientApp::loginButtonCallback),
                         callbackInfo);
        gtk_grid_attach(GTK_GRID(grid), Login_button, 0, 3, 2, 1);

        GtkWidget * register_button = gtk_button_new_with_label("Register");
        g_signal_connect(register_button, "clicked",
                         G_CALLBACK(ClientApp::registerButtonCallback),
                         callbackInfo);
        gtk_grid_attach(GTK_GRID(grid), register_button, 0, 4, 2, 1);

        Quit_button = gtk_button_new_with_label("Quit");
        g_signal_connect_swapped(Quit_button, "clicked",
                                 G_CALLBACK(gtk_widget_destroy), window);
        //g_signal_connect(Quit_button, "clicked", G_CALLBACK(exit), NULL);
        gtk_grid_attach(GTK_GRID(grid), Quit_button, 0, 5, 2, 1);

        return window;
    }

    //---------------------------------------------------------//

    GtkWidget* ClientApp::getAcessDeniedWindow()
    {
        GtkWidget* window;
        GtkWidget* grid;
        GtkWidget* Quit_button;
        GtkWidget* button_container;

        window = gtk_application_window_new(m_app);
        gtk_window_set_title(GTK_WINDOW(window), "AcessDenied");
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        gtk_container_set_border_width(GTK_CONTAINER(window), 10);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

        grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_container_add(GTK_CONTAINER(window), grid);

        GtkWidget* label = gtk_label_new("AcessDenied");
        gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

        Quit_button = gtk_button_new_with_label("Ok");
        g_signal_connect_swapped(Quit_button, "clicked",
                                 G_CALLBACK(gtk_widget_destroy), window);
        gtk_grid_attach(GTK_GRID(grid), Quit_button, 0, 2, 1, 1);

        return window;

        //GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "AcessDenied");
        //gtk_window_set_title(GTK_WINDOW(dialog), "AcessDenied");

        //return dialog;
    }

    //---------------------------------------------------------//

    GtkWidget* ClientApp::getMainWindow()
    {
        GtkWidget* window;
        GtkWidget* button;
        GtkWidget* button_box;

        window = gtk_application_window_new(m_app);
        gtk_window_set_title(GTK_WINDOW(window), "Window");
        // gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        gtk_container_set_border_width(GTK_CONTAINER(window), 10);
        gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

        // GtkWidget* scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        // GtkWidget* child_widget = gtk_button_new();

        gtk_drag_dest_set(window, GTK_DEST_DEFAULT_ALL, targetentries, 1,
                          GDK_ACTION_COPY);
        g_signal_connect(window, "drag-data-received",
                         G_CALLBACK(on_drag_data_received), this);

        GtkWidget* grid = gtk_grid_new();
        // gtk_grid_set_row_spacing(GTK_GRID(grid), );
        gtk_container_add(GTK_CONTAINER(window), grid);

        GtkWidget* Quit_button = gtk_button_new_with_label("Quit");
        gtk_widget_set_name(Quit_button, "button_red");
        buttonEnableCSS(Quit_button);
        g_signal_connect_swapped(Quit_button, "clicked",
                                 G_CALLBACK(gtk_widget_destroy), window);
        //g_signal_connect(Quit_button, "clicked", G_CALLBACK(exit), NULL);
        // gtk_container_add(GTK_CONTAINER(window), Quit_button);
        gtk_grid_attach(GTK_GRID(grid), Quit_button, 1, 1, 1, 1);

        std::string userdir = ClientApp::s_mainDir + "/" + m_login + "/";

        uint32_t button_index = 2;
        std::lock_guard<std::mutex> lock(m_mtx_file_list);
        for (auto file : m_file_list)
        {
            GtkWidget* button = gtk_button_new_with_label(file.c_str());
            std::string fullpath = userdir + file;
            const bool is_file = File::checkFileExistance(fullpath);
            if (is_file)
            {
                gtk_widget_set_name(button, "button_green");
            }
            else
            {
                gtk_widget_set_name(button, "button_gray");
            }
            buttonEnableCSS(button);

            g_signal_connect(button, "clicked",
                                G_CALLBACK(ClientApp::fileButtonCallback),
                                this);
            gtk_grid_attach(GTK_GRID(grid), button, 1, button_index, 1, 1);

            if (is_file)
            {
                std::string remove_str = "remove " + file;
                button = gtk_button_new_with_label(remove_str.c_str());
                gtk_widget_set_name(button, "button_red");
                buttonEnableCSS(button);
                g_signal_connect(button, "clicked",
                                    G_CALLBACK(ClientApp::fileRemoveButtonCallback),
                                    this);
                
                gtk_grid_attach(GTK_GRID(grid), button, 2, button_index, 1, 1);
            }
            ++button_index;
        }

        return window;
    }

    //---------------------------------------------------------//

    void ClientApp::refreshMainWindow()
    {
        std::lock_guard<std::mutex> g(m_renderMtx);
        if (nullptr != m_window)
        {
            gtk_widget_destroy(m_window);
            m_window = nullptr;
        }

        m_window = getMainWindow();
        gtk_widget_show_all(m_window);
    }

    //////////////////////////////////////////////////////////////

    void ClientApp::enableCSS()
    {
        m_provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data(m_provider,
            "* #button_red{\
                    background-color: FireBrick;\
                    font-size: large;\
                    color: black;\
                    font-weight: bold;\
                    background-image:none;\
                }\
                \
                #button_gray{\
                    background-color: gray;\
                    font-size: large;\
                    color: black;\
                    font-weight: bold;\
                    background-image:none;\
                }\
                \
                #button_green{\
                    background-color: green;\
                    font-size: large;\
                    color: black;\
                    font-weight: bold;\
                    background-image:none;\
                }"
        ,-1,NULL);
    }

    //---------------------------------------------------------//

    void ClientApp::buttonEnableCSS(GtkWidget* button)
    {
        GtkStyleContext* context = gtk_widget_get_style_context(button);
        gtk_style_context_add_provider(
            context,
            GTK_STYLE_PROVIDER(m_provider),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

    //---------------------------------------------------------//

    void ClientApp::activate(GtkApplication* app, gpointer user_data)
    {
        ClientApp* clientApp = (ClientApp*)user_data;

        std::lock_guard<std::mutex> g(clientApp->m_renderMtx);
        clientApp->m_window = clientApp->getLoginWindow();

        gtk_widget_show_all(clientApp->m_window);
    }

    //---------------------------------------------------------//

    void ClientApp::registerButtonCallback(GtkWidget* button, gpointer data)
    {
        LoginCallbckData* callbackData = (LoginCallbckData*)data;

        const std::string login =
            gtk_entry_get_text(GTK_ENTRY(callbackData->m_loginWidget));
        const std::string password =
            gtk_entry_get_text(GTK_ENTRY(callbackData->m_passwWidget));

        ClientApp* app = callbackData->m_app;

        app->m_login = login;

        File::createdir(ClientApp::s_mainDir + "/" + login);

        app->netRegister(login, password);

        app->m_loginEvent.wait();

        if (app->m_login.size() == 0)
        {
            GtkWidget* window = app->getAcessDeniedWindow();
            gtk_widget_show_all(window);
        }
        else
        {
            delete callbackData;
            app->refreshMainWindow();
        }
    }

    //---------------------------------------------------------//

    void ClientApp::loginButtonCallback(GtkWidget* button, gpointer data)
    {
        LoginCallbckData* callbackData = (LoginCallbckData*)data;

        const std::string login =
            gtk_entry_get_text(GTK_ENTRY(callbackData->m_loginWidget));
        const std::string password =
            gtk_entry_get_text(GTK_ENTRY(callbackData->m_passwWidget));

        ClientApp* app = callbackData->m_app;

        app->m_login = login;

        File::createdir(ClientApp::s_mainDir + "/" + login);

        app->netLogin(login, password);

        app->m_loginEvent.wait();

        if (app->m_login.size() == 0)
        {
            //GtkWidget* window = app->getAcessDeniedWindow();
            //gint result = gtk_dialog_run(GTK_DIALOG(window));
            //gtk_widget_destroy( GTK_WIDGET(window) );

            GtkWidget* window = app->getAcessDeniedWindow();
            gtk_widget_show_all(window);
        }
        else
        {
            delete callbackData;
            app->refreshMainWindow();
        }
    }

    //---------------------------------------------------------//

    void ClientApp::fileButtonCallback(GtkWidget* button, gpointer data)
    {
        ClientApp* app = (ClientApp*)data;

        std::string label = gtk_button_get_label(GTK_BUTTON(button));

        app->netReadFile(label);

        app->m_readEvent.wait();

        app->refreshMainWindow();
    }

    //---------------------------------------------------------//

    void ClientApp::fileRemoveButtonCallback(GtkWidget* button, gpointer data)
    {
        ClientApp* app = (ClientApp*)data;

        std::string label = std::string(gtk_button_get_label(GTK_BUTTON(button))).substr(7);

        {
            std::lock_guard<std::mutex> lock(app->m_mtx_file_list);
            app->m_file_list.remove(label);
            std::string fullpath = ClientApp::s_mainDir + "/" + app->m_login + "/" + label;
            File::removeFile(fullpath);
        }

        app->netRemoveFile(label);

        app->refreshMainWindow();
    }

    //---------------------------------------------------------//

    void ClientApp::on_drag_data_received(GtkWidget* wgt,
                                          GdkDragContext* context, gint x,
                                          gint y, GtkSelectionData* seldata,
                                          guint info, guint time, gpointer data)
    {
        ClientApp* app = (ClientApp*)data;

        gchar** filenames = NULL;
        filenames = g_uri_list_extract_uris(
            (const gchar*)gtk_selection_data_get_data(seldata));
        if (filenames == NULL)
            {
                gtk_drag_finish(context, FALSE, FALSE, time);
                return;
            }
        int iter = 0;
        while (filenames[iter] != NULL)
            {
                std::string filename =
                    g_filename_from_uri(filenames[iter], NULL, NULL);

                app->netWriteFile(filename);

                app->m_writeEvent.wait();

                iter++;
            }
        gtk_drag_finish(context, TRUE, FALSE, time);

        app->refreshMainWindow();
    }

    //---------------------------------------------------------//

} // namespace TB