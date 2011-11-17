/** @file session.c
    FTP client session (connection) support */


#include "reefs.h"


/******************************************************************************
 * Processing FTP commands
 */

int process_USER(struct session* ses, const char* data)
{
    if (!ses)   { errno = EFAULT; return -1; }

    strncpy (ses->login, data, MAX_LOGIN);
    ses->logged_in = 0;

    respond (ses, 331, "Please specify the password.");
    return 0;
}

int process_PASS(struct session* ses, const char* data)
{
    if (!ses)   { errno = EFAULT; return -1; }

    if (strlen(ses->login) == 0)
        respond (ses, 503, "Login with USER first.");
    else
    {
        if (strcmp(ses->login, "anonymous") == 0 || strcmp(ses->login, "ftp") == 0)
            ses->logged_in = (strstr(data, "@") != NULL);
        else
        {
            // search users for a match
            int i;
            for (i = 0; i < ses->server->config.users_count; ++i)
                if (strcmp(ses->login, ses->server->config.users[i].login) == 0
                    && strcmp(data, ses->server->config.users[i].password) == 0)
                    ses->logged_in = 1;
        }
    }

    if (ses->logged_in) respond (ses, 230, "Login successful.");
    else                respond (ses, 530, "Login incorrect.");

    return 0;
}

int process_QUIT(struct session* ses, const char* data)
{
    respond (ses, 221, "Goodbye.");
    ses->terminated = 1;

    return 0;
}

int process_FEAT(struct session* ses, const char* data)
{
    static const char features[] = "Features:\nPASV\nEnd";
    respond (ses, 211, features);

    return 0;
}

int process_SYST(struct session* ses, const char* data)
{
    respond (ses, 215, "UNIX Type: L8");
    return 0;
}


int process_PWD(struct session* ses, const char* data)
{
    char dir[MAX_PATH];
    if (strlen(ses->current_dir) == 0)  strcpy(dir, "/");
    else absolute_to_relative_path(ses->server->config.root_dir, ses->current_dir, dir);

    char resp[MAX_PATH+2];
    snprintf (resp, MAX_PATH+2, "\"%s\"", dir);
    respond (ses, 257, resp);
    return 0;
}

int process_CDUP(struct session* ses, const char* data)
{
    respond (ses, 550, "Operation not supported.");
    return 0;
}

int process_CWD(struct session* ses, const char* data)
{
    int ok = 0;

    if (strlen(data) > 0)
    {
        if (strcmp(data, ".") == 0)
            ok = 1; // no-op
        else if (strcmp(data, "..") == 0)
            return process_CDUP(ses, data);
        else
        {
            const char* ref = *data == '/' ? ses->server->config.root_dir : ses->current_dir;

            char dir[MAX_PATH];
            if (relative_to_absolute_path(ref, data, dir))
            {
                struct stat st;
                if (lstat(dir, &st) != -1 && S_ISDIR(st.st_mode))
                {
                    strncpy (ses->current_dir, dir, MAX_PATH);
                    ok = 1;
                }
            }
        }
    }

    if (ok) respond (ses, 250, "Directory successfully changed.");
    else    respond (ses, 550, "Failed to change directory.");
    return 0;
}

int process_MKD(struct session* ses, const char* data)
{
    if (strlen(data) > 0)
    {
        char dir[MAX_PATH];
        if (relative_to_absolute_path(ses->current_dir, data, dir))
            if (mkdir(dir, 0755) != -1)
            {
                respond (ses, 257, "Directory created.");
                return 0;
            }
    }

    respond (ses, 550, "Create directory operation failed.");
    return 0;
}

int process_RMD(struct session* ses, const char* data)
{
    if (strlen(data) > 0)
    {
        char dir[MAX_PATH];
        if (relative_to_absolute_path(ses->current_dir, data, dir))
            if (rmdir(dir) != -1)
            {
                respond (ses, 250, "Remove directory operation successful.");
                return 0;
            }
    }

    respond (ses, 550, "Remove directory operation failed.");
    return 0;
}


int process_DELE(struct session* ses, const char* data)
{
    char path[MAX_PATH];
    snprintf (path, MAX_PATH, "%s/%s", ses->current_dir, data);

    // we need to check whether file exists before unlinking,
    // because otherwise we might remove a directory
    // (POSIX standard permits unlink() to behave like that)
    struct stat st;
    if (lstat(path, &st) != -1 && S_ISREG(st.st_mode))
        if (unlink(path) != -1)
        {
            respond (ses, 250, "Delete operation successful.");
            return 0;
        }

    respond (ses, 550, "Delete operation failed.");
    return 0;
}

int process_RNFR(struct session* ses, const char* data)
{
    char path[MAX_PATH];
    snprintf (path, MAX_PATH, "%s/%s", ses->current_dir, data);
    struct stat st;
    if (lstat(path, &st) == -1)
        respond (ses, 550, "RNFR command failed."); // file doesn't exist
    else
        respond (ses, 350, "Ready for RNTO.");

    return 0;
}

int process_RNTO(struct session* ses, const char* data)
{
    if (strcmp(ses->last_cmd, "RNFR") != 0)
        respond (ses, 503, "RNFR required first.");
    else
    {
        char src[MAX_PATH], dest[MAX_PATH];
        snprintf (src, MAX_PATH, "%s/%s", ses->current_dir, ses->last_cmd_data);
        snprintf (dest, MAX_PATH, "%s/%s", ses->current_dir, data);

        if (rename(src, dest) == -1)    respond (ses, 550, "Rename failed.");
        else                            respond (ses, 250, "Rename successful.");
    }

    return 0;
}


int process_TYPE(struct session* ses, const char* data)
{
    if (strlen(data) > 0)
        switch (*data)
        {
            case 'I':
            case 'i':
                // binary
                ses->data_conn.type = TYPE_BINARY;
                respond (ses, 200, "Switching to Binary mode.");
                return 0;

            case 'A':
            case 'a':
                // ASCII
                ses->data_conn.type = TYPE_ASCII;
                respond (ses, 200, "Switching to ASCII mode.");
                return 0;
        }

    respond (ses, 500, "Unrecognized TYPE command.");
    return 0;
}

int process_PASV(struct session* ses, const char* data)
{
    int sfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sfd != -1)
    {
        // find a free port (in high range) to bind the socket to
        uint16_t port = (uint16_t)(rand() % (65535 - MIN_PASV_PORT)) + MIN_PASV_PORT;
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        do {
            ++port;
            sin.sin_port = htons(port);

            if (port == 0)  goto Fail;
        } while (bind(sfd, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) == -1 && errno == EADDRINUSE);

        if (listen(sfd, BACKLOG) != -1)
        {
            socklen_t addr_len = sizeof(struct sockaddr_in);
            if (getpeername(ses->control_socket, (struct sockaddr*)&sin, &addr_len) != -1)
            {
                char ip[MAX_IPv4_LEN];
                strncpy (ip, inet_ntoa(sin.sin_addr), MAX_IPv4_LEN);
                char* p; for (p = ip; *p; ++p)    if (*p == '.') *p = ',';    // dots to commas

                char buf[BUF_LEN];
                snprintf (buf, BUF_LEN, "Entering Passive Mode (%s,%i,%i)", ip, port / 256, port % 256);
                respond (ses, 227, buf);

                ses->data_socket = sfd;
                ses->data_conn.port = port;
                ses->data_conn.mode = MODE_PASSIVE;
                return 0;
            }
        }
    }

Fail:
    respond (ses, 500, "Switching to Passive Mode failed.");
    TEMP_FAILURE_RETRY (close(sfd));
    return 0;
}


int process_LIST(struct session* ses, const char* data)
{
    if (open_data_connection(ses) != -1)
    {
        // listing directory via  `ls` command piped to temporary file
        char cmd[2*MAX_PATH];
        snprintf (cmd, 2*MAX_PATH, "ls %s \"%s\" | tail -n+2 >%s", LIST_LS_PARAMS, ses->current_dir, LIST_LS_OUTFILE);
        if (system(cmd) != -1)
        {
            respond (ses, 150, "Here comes the directory listing.");
            if (send_file(ses, LIST_LS_OUTFILE) != -1)
            {
                respond (ses, 226, "Directory send OK.");
                close_data_connection (ses);

                unlink (LIST_LS_OUTFILE);
                return 0;
            }
        }
    }

    respond (ses, 550, "Directory listing failed.");
    return 0;
}

int process_RETR(struct session* ses, const char* data)
{
    if (strlen(data) > 0)
    {
        char file[MAX_PATH];
        if (relative_to_absolute_path(ses->current_dir, data, file))
        {
            struct stat st;
            if (lstat(file, &st) != -1)
                if (open_data_connection(ses) != -1)
                {
                    char buf[BUF_LEN];
                    snprintf (buf, BUF_LEN, "Opening BINARY mode data connection for %s.", data);
                    respond (ses, 150, buf);

                    if (send_file(ses, file) != -1) respond (ses, 226, "Transfer complete.");
                    else                            respond (ses, 550, "Transfer failed.");

                    close_data_connection (ses);
                    return 0;
                }
        }
    }

    respond (ses, 550, "Failed to open file.");
    return 0;
}

int process_STOR(struct session* ses, const char* data)
{
    if (strlen(data) > 0)
    {
        char file[MAX_PATH];
        if (relative_to_absolute_path(ses->current_dir, data, file))
            if (open_data_connection(ses) != -1)
            {
                char buf[BUF_LEN];
                snprintf (buf, BUF_LEN, "Opening BINARY mode data connection for %s.", data);
                respond (ses, 150, buf);

                if (receive_file(ses, file) != -1)  respond (ses, 226, "Transfer complete.");
                else                                respond (ses, 550, "Transfer failed.");

                close_data_connection (ses);
                return 0;
            }
    }

    respond (ses, 553, "Could not create file.");
    return 0;
}

/*****************************************************************************/

// pointer to function that processed FTP command
typedef int (*FTP_CMD_PROC)(struct session*, const char* data);

// mapping of FTP commands to functions that process them
#define FC(x)  { #x, process_##x }
const struct { const char* cmd; FTP_CMD_PROC proc; }
FTP_CMD_PROCES[] = {
    FC(USER), FC(PASS), FC(QUIT),
    FC(FEAT), FC(SYST),
    FC(PWD), FC(CDUP), FC(CWD), FC(MKD), FC(RMD),
    FC(DELE), FC(RNFR), FC(RNTO),
    FC(TYPE), FC(PASV), //FC(PORT),
    FC(LIST), FC(RETR), FC(STOR),
};

int process_ftp_command(struct session* ses, const char* cmd)
{
    if (!ses || !cmd)   { errno = EFAULT; return -1; }

    int cmd_len = strlen(cmd);
    char* buf = (char*)malloc((cmd_len + 1) * sizeof(char));
    if (!buf)   return -1;
    strcpy (buf, cmd);

    // change first whitespace to \0 in order to extract command
    char* p;
    for (p = buf; *p; ++p)
        if (isspace(*p))  { *p = '\0'; break; }
    char* cmd_data = buf + strlen(buf) + 1;

    // look up the command in table
    int i;
    for (i = 0; i < ARRAY_LEN(FTP_CMD_PROCES); ++i)
        if (strcmp(buf, FTP_CMD_PROCES[i].cmd) == 0)
        {
            if ((*FTP_CMD_PROCES[i].proc)(ses, cmd_data) == -1)
                return -1;

            strncpy (ses->last_cmd, buf, MAX_FTP_CMD_LEN);
            strncpy (ses->last_cmd_data, cmd_data, MAX_PATH);
            return 0;
        }

    return -1;
}


/******************************************************************************
 * Worker functions for threads
 */

int send_welcome_message(struct session* ses)
{
    static const char motd[] = "REEFS\n(Rather Eerie Example of FTP Server)\nv%s\n"
                               "End of MOTD";

    char buf[BUF_LEN];
    snprintf (buf, BUF_LEN, motd, VERSION);
    return respond(ses, 211, buf);
}

int open_data_connection(struct session* ses)
{
    if (!ses)                       { errno = EFAULT; return -1; }
    if (ses->data_socket == -1)     { errno = EBADF;  return -1; }

    switch (ses->data_conn.mode)
    {
        case MODE_NONE:
            respond (ses, 425, "Use PORT or PASV first.");
            errno = EINVAL;
            return -1;

        case MODE_ACTIVE:
            errno = EINVAL; // active mode is not supported
            return -1;

        case MODE_PASSIVE:
        {
            int sfd = TEMP_FAILURE_RETRY(accept(ses->data_socket, NULL, NULL));
            if (sfd == -1)  return -1;

            // replacing the listening socket with data connection socket
            if (TEMP_FAILURE_RETRY(close(ses->data_socket)) == -1)  return -1;
            ses->data_socket = sfd;
        }
        return 0;

        default:            return -1;
    }
}

int close_data_connection(struct session* ses)
{
    if (!ses)   { errno = EFAULT;   return -1; }

    shutdown (ses->data_socket, SHUT_RDWR);
    TEMP_FAILURE_RETRY(close(ses->data_socket));

    ses->data_conn.mode = MODE_NONE;
    ses->data_socket = -1;
    return 0;
}

int send_file(struct session* ses, const char* file)
{
    if (!ses)                   { errno = EFAULT; return -1; }
    if (ses->data_socket == -1) { errno = EBADF; return -1; }

    int fd = TEMP_FAILURE_RETRY(open(file, O_RDONLY));
    if (fd != -1)
    {
        char buf[BUF_LEN];
        ssize_t c;

        while ((c = read_data(fd, buf, BUF_LEN)) != -1)
        {
            c = write_data(ses->data_socket, buf, c);
            if (c == -1)
            {
                if (errno == EPIPE || errno == ECONNRESET)  ses->terminated = 1;
                else                                        return -1;
            }
            if (c < BUF_LEN) break;
        }
    }

    return TEMP_FAILURE_RETRY(close(fd));
}

int receive_file(struct session* ses, const char* file)
{
    if (!ses)                   { errno = EFAULT; return -1; }
    if (ses->data_socket == -1) { errno = EBADF; return -1; }

    int fd = TEMP_FAILURE_RETRY(open(file, O_WRONLY | O_CREAT, 0755));
    if (fd == -1)   return -1;

    char buf[BUF_LEN];
    ssize_t c = -1;

    fd_set fds;
    int res;
    for (;;)
    {
        FD_ZERO (&fds); FD_SET (ses->data_socket, &fds);
        res = select(ses->data_socket + 1, &fds, NULL, NULL, NULL);
        if (res == -1)
        {
            if (errno != EINTR) return -1;
            continue;
        }
        if (res == 0)   continue;

        c = read_data(ses->data_socket, buf, BUF_LEN);
        if (c == 0 || c == -1)  break; // end of file (connection) or error

        if (write_data(fd, buf, c) < c) return -1;
    }

    if (TEMP_FAILURE_RETRY(close(fd)) == -1)    return -1;
    return c;
}

/*****************************************************************************/

/** Main loop for thread that services the control connection of FTP session. */
int control_thread_loop(struct control_thread_info* cti)
{
    int sfd = cti->session->control_socket;
    fd_set fds;
    int res;

    while (!cti->session->terminated && !terminating)
    {
        FD_ZERO (&fds); FD_SET (sfd, &fds);
        res = select(sfd + 1, &fds, NULL, NULL, NULL);
        if (res == -1)
        {
            if (errno != EINTR) FATAL("Waiting for input on control connection socket.");
            continue;
        }
        if (res == 0)   continue;

        char* line = read_line(sfd);
        if (!line || strlen(line) == 0)
        {
            free(line);
            respond (cti->session, 500, "Connection lost.");
            cti->session->terminated = 1;
            break;
        }
        log_command (cti->session, line);

        if (process_ftp_command(cti->session, line) == -1)
            respond (cti->session, 500, "Unknown or invalid command.");
    }

    return 0;
}

/** Worker function for thread that manages the control connection of FTP session. */
void* control_thread_proc(void* info)
{
    struct control_thread_info* cti = (struct control_thread_info*)info;

    // set appropriate signal mask for this thread
    sigset_t sigs;  sigemptyset (&sigs);
    sigaddset (&sigs, SIGINT);
    sigaddset (&sigs, SIGPIPE);
    pthread_sigmask (SIG_BLOCK, &sigs, NULL);

    if (send_welcome_message(cti->session) == -1)
        cti->session->terminated = 1;

    control_thread_loop (cti);

    char buf[MAX_PATH];
    snprintf (buf, MAX_PATH, "Client `%s` disconnected.", cti->session->ip_address);
    log_event (cti->session->server, buf);

    // end the control connection
    int sfd = cti->session->control_socket;
    shutdown (sfd, SHUT_RDWR);
    TEMP_FAILURE_RETRY(close(sfd));

    // end the data connection if any
    int dfd = cti->session->data_socket;
    if (!(dfd < 0))
    {
        shutdown (dfd, SHUT_RDWR);
        TEMP_FAILURE_RETRY(close(dfd));
    }

    free (cti);
    return 0;
}


/******************************************************************************
 * FTP session functions
 */

/** Accepts client connection and saves its data to given session struct. */
int new_session(int sfd, struct session* ses)
{
    if (sfd < 0)    { errno = EINVAL; return -1; }
    if (!ses)       { errno = EFAULT; return -1; }

    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    client_fd = TEMP_FAILURE_RETRY(accept(sfd, (struct sockaddr*)&client_addr, &client_addr_len));
    if (client_fd == -1)    return -1;

    ses->control_socket = client_fd;
    ses->data_socket = -1;          // no data connection initially
    strncpy (ses->ip_address, inet_ntoa(client_addr.sin_addr), MAX_IPv4_LEN);
    ses->logged_in = 0;
    *(ses->current_dir) = '\0';
    *(ses->last_cmd) = '\0';
    *(ses->last_cmd_data) = '\0';
    ses->terminated = 0;

    return 0;
}

/** Initiates client session, which includes firing up the thread for control connection. */
int start_session(struct session* ses)
{
    struct control_thread_info* cti = (struct control_thread_info*)malloc(sizeof(struct control_thread_info));
    if (!cti)   return -1;
    cti->session = ses;

    pthread_attr_t attr;
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&(ses->control_thread), &attr, control_thread_proc, (void*)cti) == -1)
        { free(cti); return -1; }

    char buf[BUF_LEN];
    snprintf (buf, BUF_LEN, "Client `%s` connected.", ses->ip_address);
    log_event (ses->server, buf);

    return 0;
}

/*****************************************************************************/

int respond(struct session* ses, int code, const char* resp)
{
    if (!ses || !resp)                  { errno = EFAULT; return -1; }
    if (code < 0 || code > 999)         { errno = EINVAL; return -1; }

    // convert response code to text
    char code_str[4];
    code_str[0] = (code / 100) + '0';   code %= 100;
    code_str[1] = (code / 10) + '0';    code %= 10;
    code_str[2] = code + '0';
    code_str[3] = '\0';

    int len = strlen(resp); if (len < 4) len = 4;
    char* buf = (char*)malloc((2 * len + 1) * sizeof(char));
    if (!buf)   return -1;

    // count lines in response
    int lines = 1; const char* p;
    for (p = resp; *p; ++p) if (*p == '\n')
        ++lines;

    // response format requires having a correct prefixing of response lines:
    // - for single line: code + space
    // - for multiline: code + dash in first line, space in following ones, code + space on last line
    int i, j = 0;
    for (i = 0; i < lines; ++i)
    {
        if (i == 0 || i + 1 == lines)
        {
            strcpy (buf + j, code_str); j += strlen(code_str);
            buf[j++] = (i == 0 && lines > 1 ? '-' : ' ');
        }
        else    buf[j++] = ' ';

        for (; *resp && *resp != '\n'; ++resp)  buf[j++] = *resp;
        buf[j++] = '\n';
        ++resp; // jump through \n in response
    }
    buf[j] = '\0';  // this \0 goes to log but not to client

    if (write_data(ses->control_socket, buf, (size_t)j) < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)  ses->terminated = 1;
        else                                        return -1;
    }
    if (log_response(ses, buf) == -1)                           return -1;

    free (buf);
    return 0;
}
