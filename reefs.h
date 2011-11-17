/** @file reefs.h
    Main header file */


#ifndef REEFS__H
#define REEFS__H

#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/utsname.h>


#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))


/******************************************************************************
 * Constants
 */

#define VERSION "0.5.1"
#define BACKLOG 5

#define BUF_LEN 256
#define MAX_LOGIN 64
#define MAX_PASSWORD 128
#define MAX_IPv4_LEN (16+1)
#define MAX_FTP_CMD_LEN (4+1)

#ifdef PATH_MAX
#define MAX_PATH (PATH_MAX+1)
#else
#define MAX_PATH (1024+1)
#endif

#define DEFAULT_CONFIG_FILE "./config"
#define DEFAULT_USERS_FILE "./users"
#define DEFAULT_LOG_FILE "./log"
#define DEFAULT_ROOT_DIR "/var/lib/ftp"
#define DEFAULT_LISTEN_PORT 21


#define MIN_PASV_PORT 10384

// FTP connection modes
#define MODE_NONE 0
#define MODE_ACTIVE 1
#define MODE_PASSIVE 2

// FTP transmission types
#define TYPE_BINARY 'I'
#define TYPE_ASCII 'A'


#define LIST_LS_PARAMS "--almost-all -n"
#define LIST_LS_OUTFILE "/var/tmp/reefs-list"


/******************************************************************************
 * Structs
 */

struct user
{
    char login[MAX_LOGIN];
    char password[MAX_PASSWORD];
};

struct config
{
    char config_file[MAX_PATH];
    char users_file[MAX_PATH];
    char log_file[MAX_PATH];

    char root_dir[MAX_PATH];    // root directory of the server
    short port;
    int max_clients;            // 0 = no limit

    struct user* users;         // login data for users
    int users_count;
};

struct server
{
    struct config config;

    int listen_socket;          // listens on config.port
    int log_fd;
};

// contains info about FTP client session
// (note: once control connection thread is started, nothing else shall modify this struct)
struct session
{
    const struct server* server;

    pthread_t control_thread;

    int control_socket;
    int data_socket;
    struct
    {
        int type;                   // transmission type
        int mode;                   // connection mode (passive or active)
        uint16_t port;              // listening port (passive) or destination port for connecting (active)
        uint32_t ip;                // destination IP (active only)
    } data_conn;

    // client info
    int logged_in;
    char login[MAX_LOGIN];
    char ip_address[MAX_IPv4_LEN];
    char current_dir[MAX_PATH];

    char last_cmd[MAX_FTP_CMD_LEN];
    char last_cmd_data[MAX_PATH];
    int terminated;
};


struct control_thread_info
{
    struct session* session;
};


/******************************************************************************
 * Functions
 */

int trim(char* text);
char** split_by_whitespaces(const char* text, int* segs);
int free_strings(char** array, int size);

ssize_t read_data(int fd, char* buf, size_t count);
ssize_t write_data(int fd, const char* buf, size_t count);
char* read_line(int fd);
char* absolute_to_relative_path(const char* base, const char* target, char* out);
char* relative_to_absolute_path(const char* base, const char* target, char* out);

int load_config(const char* file, struct config*);

int init_server(const char* config_file, struct server*);
int start_server(struct server*);
int stop_server(struct server*);

int new_session(int sfd, struct session*);
int start_session(struct session*);
int respond(struct session*, int code, const char* resp);

int open_data_connection(struct session* ses);
int close_data_connection(struct session* ses);
int send_file(struct session* ses, const char* file);
int receive_file(struct session* ses, const char* file);

int log_line(int logfd, const char* line);
int log_command(struct session*, const char* cmd);
int log_response(struct session*, const char* resp);
int log_event(const struct server* serv, const char* msg);

void error(const char* file, int line, const char* msg);
#define ERROR(msg)  error(__FILE__, __LINE__, (msg))
#define FATAL(msg)  { ERROR(msg); exit(EXIT_FAILURE); }


/******************************************************************************
 * Global variables
 */

extern volatile sig_atomic_t terminating; // whether server was terminated by SIGINT

#endif // REEFS__H
