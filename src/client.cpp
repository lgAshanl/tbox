#include "tb_clientapp.h"
#include "tbserver.h"

int main(int argc, char* argv[])
{
#if 0

    TB::NetServer server;
    server.init();
    server.start();
#endif

    TB::ClientApp app;
    app.init();

    int status = app.start(argc, argv);
    app.deinit();

    return 0;
}