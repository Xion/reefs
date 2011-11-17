/** @file config.c
    Code for loading the configuration file */


#include "reefs.h"


/******************************************************************************
 * Text & file handling functions
 */

int trim(char* text)
{
    if (!text)  { errno = EFAULT; return -1; }

    int i;
    for (i = 0; text[i]; )
        if (isspace(text[i]))   text[i] = text[i + 1];
        else                    ++i;

    for (i = strlen(text) - 1; isspace(text[i]); --i)
        text[i] = '\0';

    return 0;
}

/** Splits the text by whitespaces and returns array of segements. */
char** split_by_whitespaces(const char* text, int* segs)
{
    if (!text)  { errno = EFAULT; return NULL; }

    char** res = NULL;
    const char *p, *pp;
    *segs = 0;
    for (p = text; *p; ++p)
        if (isspace(*p))  continue;
        else
        {
            for (pp = p; *pp && !isspace(*pp); ++pp)  { } // find the end of segment

            if (!(res = (char**)realloc(res, (*segs + 1) * sizeof(char*)))) return 0;
            if (!(res[*segs] = (char*)malloc((pp - p + 1) * sizeof(char)))) return 0;
            int seg_len = pp - p;
            memcpy (res[*segs], p, seg_len * sizeof(char));
            res[*segs][seg_len] = '\0';

            p = pp - 1;
            ++*segs;
        }

    // special case: one empty segment
    if (!res)
    {
        if (!(res = (char**)malloc(sizeof(char*))))     return 0;
        if (!(*res = (char*)malloc(sizeof(char))))    return 0;
        **res = '\0';
        return res;
    }

    return res;
}

int free_strings(char** array, int size)
{
    if (!array) { errno = EFAULT; return -1; }

    int i;
    for (i = 0; i < size; ++i)
        free (array[i]);
    free (array);

    return 0;
}

/*****************************************************************************/

/** Reads from file, handling the possibility of signal interruption. */
ssize_t read_data(int fd, char* buf, size_t count)
{
    int c;
    size_t len = 0;

    do
    {
        c = TEMP_FAILURE_RETRY(read(fd,buf,count));
        if (c < 0) return c;
        if (c == 0) return len;

        buf += c;
        len += c;
        count -= c;
     } while (count > 0);

     return len;
}

/** Writes to file, handling the possibility of signal interruption. */
ssize_t write_data(int fd, const char* buf, size_t count)
{
    int c;
    size_t len = 0;

    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0) return c;

        buf += c;
        len += c;
        count -= c;
    } while (count > 0);

    return len;
}

/** Reads a line from file. Result is allocated on heap and should be freed by caller. */
char* read_line(int fd)
{
    int len = 0, cap = 1;
    char* res;
    char in;
    ssize_t c;

    if (!(res = (char*)malloc(sizeof(char))))   return 0;
    *res = '\0';
    for (;;)
    {
        c = read_data(fd, &in, 1);
        if (c <= 0)             { free (res); return NULL; }    // end of file
        if (!in || in == '\n')  return res;                     // end of line

        if (in == '\r')
        {
            read_data(fd, &in, 1);
            return res;
        }

        res[len++] = in;
        while (len >= cap)  cap *= 2;
        if (!(res = (char*)realloc(res, cap * sizeof(char))))    return 0;
        res[len] = '\0';
    }

    return res;
}

/*****************************************************************************/

/** Converts absolute path to relative. Note that it doesn't handle cases
    when result should include ".." but those doesn't happen in code for PWD handling,
    where the command is used. */
char* absolute_to_relative_path(const char* base, const char* target, char* out)
{
    if (!base || !target || !out)   { errno = EFAULT; return NULL; }

    // find path segments which are equal
    int base_len = strlen(base), target_len = strlen(target), i, j;
    for (i = 0; i < base_len && i < target_len; )
    {
        int equal = 0;
        for (j = i + 1; 1; ++j)
            if ((j == base_len || base[j] == '/') && (j == target_len || target[j] == '/'))
                { equal = 1; break; }
            else if (j == base_len || j == target_len)
                break;

        if (!equal) break;  else i = j;
    }

    // omit the qual parts and copy the rest of target path to result
    for (j = 0; i < target_len; ++i, ++j)
        out[j] = target[i];
    if (j > 0 && out[j-1] == '/')  --j;     // we don't want the trailing slash
    if (j == 0) out[j++] = '/';             // ...unless it's the only slash
    out[j] = '\0';

    return out;
}

/** Converts relative to absolute path. It doesn't handle cases where the
    relative path contains ".." but those doesn't happen in the CWD command handling,
    where this function is used. */
char* relative_to_absolute_path(const char* base, const char* target, char* out)
{
    if (!base || !target || !out)   { errno = EFAULT; return NULL; }

    strncpy (out, base, MAX_PATH);
    int i = strlen(out);

    if (*target != '/') out[i++] = '/';
    const char* p;
    for (p = target; *p; ++p, ++i)  out[i] = *p;

    if (out[i-1] == '/')    --i;
    out[i] = '\0';

    return out;
}


/******************************************************************************
 * Parsing files
 */

int is_config_command(const char* line)
{
    if (!line)  { errno = EFAULT; return -1; }

    if (line[0] == '#')         return 0;   // comment
    else if (line[0] == '\0')   return 0;   // empty line

    return 1;
}

/*****************************************************************************/

struct user* parse_users_file(const char* file, int* users_count)
{
    if (!file || !users_count)  { errno = EFAULT; return 0; }

    int fd;
    if ((fd = TEMP_FAILURE_RETRY(open(file, O_RDONLY))) == -1)    return 0;

    char* line = NULL;
    char** cmd;
    int cmd_count;
    struct user* res = NULL;
    *users_count = 0;
    for(;;)
    {
        line = read_line(fd);
        if (!line) return res;

        if (!is_config_command(line)) continue;
        if (!(cmd = split_by_whitespaces(line, &cmd_count)) || cmd_count < 2)
            { free(line); continue; }  // ignore malformed entries
        free (line);

        // add user
        if (!(res = (struct user*)realloc(res, (*users_count + 1) * sizeof(struct user))))
            { free(cmd); free(res);  return 0; }
        strncpy (res[*users_count].login, cmd[0], MAX_LOGIN);
        strncpy (res[*users_count].password, cmd[1], MAX_PASSWORD);
        ++*users_count;

        free_strings (cmd, cmd_count);
    }
}

/** Reads a single config line and modifies the supplied config struct accordingly. */
int read_config_param(char** cmd, struct config* cfg)
{
    if (!cmd || !cfg)   { errno = EFAULT; return -1; }

    if (strcmp(cmd[0], "root-directory") == 0)
        strncpy (cfg->root_dir, cmd[1], MAX_PATH);
    else if (strcmp(cmd[0], "port") == 0)
        cfg->port = atoi(cmd[1]);
    else if (strcmp(cmd[0], "max-clients") == 0)
        cfg->max_clients = atoi(cmd[1]);
    else if (strcmp(cmd[0], "users-file") == 0)
        strncpy (cfg->users_file, cmd[1], MAX_PATH);
    else if (strcmp(cmd[0], "log-file") == 0)
        strncpy (cfg->log_file, cmd[1], MAX_PATH);
    else
        { errno = EINVAL; return -1; }

    return 0;
}

int parse_config_file(const char* file, struct config* cfg)
{
    if (!file || !cfg)  { errno = EFAULT; return -1; }

    int fd;
    if ((fd = TEMP_FAILURE_RETRY(open(file, O_RDONLY))) == -1)    return -1;

    char* line = NULL;
    char** cmd;
    int cmd_count;
    for(;;)
    {
        line = read_line(fd);
        if (!line)  return 0;

        if (!is_config_command(line))   continue;
        if (!(cmd = split_by_whitespaces(line, &cmd_count)) || cmd_count < 2)
            { free(line); continue; }  // ignore malformed entries
        free (line);

        if (read_config_param(cmd, cfg) == -1)
            { free_strings(cmd, cmd_count); return -1; }
        free_strings(cmd, cmd_count);
    }

    strncpy (cfg->config_file, file, MAX_PATH);
    return 0;
}

/*****************************************************************************/

int load_config(const char* file, struct config* cfg)
{
    if (!file || !cfg)  { errno = EFAULT; return -1; }

    // set config defaults
    strncpy (cfg->config_file, file, MAX_PATH);
    strncpy (cfg->users_file, DEFAULT_USERS_FILE, MAX_PATH);
    strncpy (cfg->log_file, DEFAULT_LOG_FILE, MAX_PATH);
    strncpy (cfg->root_dir, DEFAULT_ROOT_DIR, MAX_PATH);
    cfg->port = DEFAULT_LISTEN_PORT;
    cfg->max_clients = 0;
    cfg->users_count = 0;

    if (parse_config_file (file, cfg) != -1)
    {
        // expand relative paths
        char* path;
        if ((path = canonicalize_file_name(cfg->root_dir)) != NULL)
            { strncpy (cfg->root_dir, path, MAX_PATH);      free (path); }
        if ((path = canonicalize_file_name(cfg->config_file)) != NULL)
            { strncpy (cfg->config_file, path, MAX_PATH);   free (path); }
        if ((path = canonicalize_file_name(cfg->users_file)) != NULL)
            { strncpy (cfg->users_file, path, MAX_PATH);    free (path); }
        if ((path = canonicalize_file_name(cfg->log_file)) != NULL)
            { strncpy (cfg->log_file, path, MAX_PATH);      free (path); }
    }

    if (!(cfg->users = parse_users_file(cfg->users_file, &(cfg->users_count))))
        cfg->users_count = 0;

    return 0;
}
