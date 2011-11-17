/*  REEFS
    (Rather Eerie Example of FTP Server)
    by Karol Kuczmarski, 2008 */


#define _GNU_SOURCE
#include "reefs.h"

volatile sig_atomic_t terminating = 0;


void usage()
{
    fprintf (stdout, "%s", "usage: reefs [config-file]\n");
}


void sig_INT(int sig)   { terminating = 1; }

int main(int argc, char* argv[])
{
    char* config_file;

    switch (argc)
    {
        case 1:     config_file = DEFAULT_CONFIG_FILE;  break;
        case 2:     config_file = argv[1];              break;
        default:    usage();                            return EXIT_FAILURE;
    }

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;    if (sigaction(SIGPIPE, &sa, NULL) == -1)    FATAL("Ignoring SIGPIPE");
    sa.sa_handler = sig_INT;    if (sigaction(SIGINT, &sa, NULL) == -1)     FATAL("Handling SIGINT");

    struct server serv;
    if (init_server(config_file, &serv) == -1)
    {
        fprintf (stderr, "%s", "Error during server's initialization.\n");
        perror (":");        return EXIT_FAILURE;
    }

    srand ((unsigned)time(0));
    if (start_server(&serv) == -1)
    {
        fprintf (stderr, "%s", "Could not start server.\n");
        perror (":");        return EXIT_FAILURE;
    }

    if (stop_server(&serv) == -1)
    {
        fprintf (stderr, "%s", "Error during server's shutdown.\n");
        perror(":");         return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
