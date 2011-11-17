/** @file server.c
    High-level management of the server  */


#include "reefs.h"


/******************************************************************************
 * Starting and stopping the server
 */

/** Initializes the server, filling the server struct. */
int init_server(const char* config_file, struct server* serv)
{
    if (!config_file || !serv)  { errno = EFAULT; return -1; }
    fprintf (stdout, "REEFS v%s\n", VERSION);

    fprintf (stdout, "%s", "Loading configuration...");
    if (load_config(config_file, &(serv->config)) == -1)  return -1;
    fprintf (stdout, "%s", "OK\n");

    fprintf (stdout, "%s", "Opening log file...");
    if ((serv->log_fd = TEMP_FAILURE_RETRY(open(serv->config.log_file,
                                                O_WRONLY | O_CREAT | O_APPEND | O_DSYNC, 0666))) == -1)
        return -1;
    fprintf (stdout, "%s", "OK\n");

    fprintf (stdout, "%s", "Initializing server socket...");
    if ((serv->listen_socket = socket(PF_INET, SOCK_STREAM, 0)) == -1)    return -1;
    int reuse = 1;
    if (setsockopt(serv->listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(int)) == -1)
        return -1;

    // listen on server socket
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)serv->config.port);
    if (bind(serv->listen_socket, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1)
        return -1;
    if (listen(serv->listen_socket, BACKLOG) == -1)  return -1;
    fprintf (stdout, "%s", "OK\n");

    fprintf (stdout, "%s", "Server successfully initialized.\n");
    return 0;
}

int start_server(struct server* serv)
{
    if (!serv)  { errno = EFAULT; return -1; }

    struct session* sessions = NULL;    // array with FTP sessions info
    int sessions_count = 0;
    struct session inc_session;

    log_event (serv, "Server started.");
    fd_set fds;
    int res;
    while (!terminating)
    {
        // wait for incoming connections
        FD_ZERO (&fds); FD_SET (serv->listen_socket, &fds);
        res = select(serv->listen_socket + 1, &fds, NULL, NULL, NULL);
        if (res == -1)
        {
            if (errno != EINTR) FATAL("Waiting for incoming connections.");
            continue;
        }
        if (res == 0)   continue;

        // accept them
        if (new_session(serv->listen_socket, &inc_session) == -1)   FATAL("Accepting incoming connection");
        inc_session.server = serv;
        strncpy (inc_session.current_dir, serv->config.root_dir, MAX_PATH); // set initial directory

        // start servicing the new connection
        sessions = (struct session*)realloc(sessions, (sessions_count + 1) * sizeof(struct session));
        memcpy (&sessions[sessions_count], &inc_session, sizeof(struct session));
        ++sessions_count;
        if (start_session(&sessions[sessions_count - 1]) == -1) FATAL("Handling client session");
    }

    log_event (serv, "Server terminated.");
    return 0;
}

int stop_server(struct server* serv)
{
    if (!serv)  { errno = EFAULT; return -1; }

    if (TEMP_FAILURE_RETRY(close(serv->log_fd)) == -1)
        return -1;

    log_event (serv, "Server stopped.");
    return 0;
}


/******************************************************************************
 * Logging functions
 */

int log_line(int logfd, const char* line)
{
    if (logfd < 0)  { errno = EBADFD; return -1; }
    if (!line)      { errno = EFAULT; return -1; }
    static const char* LINE_FEED = "\n";

    int c;

    // log current time
    time_t time_val;        if (time(&time_val) == (time_t)-1)    return -1;
    struct tm time_struct;  if (localtime_r(&time_val, &time_struct) == NULL)  return -1;
    char time_buf[64];      if (asctime_r(&time_struct, time_buf) == NULL)   return -1;
    *strstr(time_buf, "\n") = ' ';
    c = strlen(time_buf);   if (write_data(logfd, time_buf, c) < c) return -1;

    // log the message
    c = strlen(line);       if (write_data(logfd, line, c) < c) return -1;
    c = strlen(LINE_FEED);  if (write_data(logfd, LINE_FEED, c) < c)    return -1;

    // duplicate output to STDOUT
    if (logfd != STDOUT_FILENO && log_line(STDOUT_FILENO, line) == -1)  return -1;

    return 0;
}

int log_command(struct session* ses, const char* cmd)
{
    if (!ses || !cmd)               { errno = EFAULT; return -1; }
    if (ses->server->log_fd < 0)    { errno = EBADFD; return -1; }

    static const int LINE_LEN = 1 + MAX_IPv4_LEN + 2 + MAX_PATH;
    char buf[LINE_LEN];
    snprintf (buf, LINE_LEN, "[%s] %s", ses->ip_address, cmd);

    return log_line(ses->server->log_fd, buf);
}

int log_response(struct session* ses, const char* resp)
{
    if (!ses || !resp)              { errno = EFAULT; return -1; }
    if (ses->server->log_fd < 0)    { errno = EBADFD; return -1; }

    int i; char* buf;
    while (*resp)
    {
        for (i = 0; resp[i] && resp[i] != '\n'; ++i) { }
        if (!(buf = (char*)malloc((i + 1) * sizeof(char)))) return -1;
        strncpy (buf, resp, i);
        buf[i] = '\0';

        if (log_command(ses, buf) == -1)    return -1;
        free (buf);
        resp += i + 1;
    }

    return 0;
}

int log_event(const struct server* serv, const char* msg)
{
    return log_line(serv->log_fd, msg);
}


void error(const char* file, int line, const char* msg)
{
    fprintf (stderr, "%s, line %d\n", file, line);
    perror (msg);
}
