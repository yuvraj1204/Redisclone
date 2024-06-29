#include <string.h>
#include <iostream>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <assert.h>
#include <fcntl.h>
#include <vector>
#include <poll.h>
#include "hashtable.h"
#include <string>
#include "avl.h"

const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

enum
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

enum
{
    ERR_UNKNOWN = 1,
    ERR_2BIG = 2,
};

// The data structure for the key space. This is just a placeholder
// until we implement a hashtable in the next chapter.
// static std::map<std::string, std::string> g_map;

// The data structure for key space
static struct
{
    HMap db;
} g_data;

struct Conn
{
    int fd = -1;
    uint32_t state = 0;

    size_t rbuf_size = 0;
    size_t rbuf_read = 0;
    uint8_t rbuf[4 + k_max_msg];

    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

// the structure for the key
struct Entry
{
    struct HNode node;
    std::string key;
    std::string val;
};

#define container_of(ptr, type, member) ({                  \
    typeof(  ((type *)0)->member ) *__mptr = ptr;           \
    (type *)( (size_t) __mptr - offsetof(type, member)); })

static bool try_fill_buffer(Conn *conn);
static void msg(const char *msg);
static void die(const char *msg);
static void fd_set_nb(int fd);
static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, char *buf, size_t n);
static int32_t one_request(int connfd);
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn);
static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int32_t fd);
static void state_res(Conn *conn);
static bool try_one_request(struct Conn *conn);
static bool try_flush_buffer(struct Conn *conn);

static void do_request(std::vector<std::string> &cmd, std::string &out);
static int32_t parse_req(const uint8_t *data, uint32_t len, std::vector<std::string> &cmd);

static void out_nil(std::string &out);
static void out_str(std::string &out, const std::string &val);
static void out_int(std::string &out, int64_t val);
static void out_err(std::string &out, int32_t code, const std::string &msg);
static void out_arr(std::string &out, uint32_t n);

static void msg(const char *msg)
{
    std::cerr << msg << std::endl;
}

static void die(const char *msg)
{
    int err = errno;
    std::cerr << msg << std::endl;
    abort();
}

static u_int64_t str_hash(const uint8_t *data, size_t len)
{
    u_int64_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++)
    {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        die("fcntl error");
    }
}

static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n); // Read exactly n bytes from buf
        if (rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n); // Write exactly n bytes into buf
        if (rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t one_request(int connfd)
{
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4); // Read 4 bytes (the header which signifies length of msg)
                                              // 4 bytes cuz length is in unit format .. which is 4 bytes

    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4); // Copy rbuf into len cuz it currently contains the length of the msg
                           // which about to be read next

    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    err = read_full(connfd, &rbuf[4], len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';
    std::cout << "client says: " << &rbuf[4] << std::endl;

    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];

    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);        // First we write the header (i.e., the length of the reply msg)
    memcpy(&wbuf[4], reply, len); // Now the real msg is copied into the write buffer

    return write_all(connfd, wbuf, len + 4);
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int32_t fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t len = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &len);

    if (connfd < 0)
    {
        msg("accept() error");
        return -1;
    }

    fd_set_nb(connfd);

    struct Conn *conn = new Conn();
    if (!conn)
    {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);

    return 0;
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
        ;
}

static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size + conn->rbuf_read < sizeof(conn->rbuf));
    int32_t rv = 0;

    // Move buffer data to the beginning
    // removing the data which has been successfully written to the write buffer
    memmove(conn->rbuf, &conn->rbuf[conn->rbuf_read], conn->rbuf_size);
    conn->rbuf_read = 0;

    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        // std::cout << "Read start" << std::endl;

        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
        // std::cout << "Read Successful" << std::endl;

    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    else if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    else if (rv == 0)
    {
        if (conn->rbuf_size > 0)
        {
            msg("EOF");
        }
        else
        {
            // msg("unexpected EOF");
        }
        return false;
    }

    conn->rbuf_size += (size_t)rv;

    assert(conn->rbuf_size <= sizeof(conn->rbuf));
    while (try_one_request(conn))
        ;
    return conn->state == STATE_REQ;
}

void print_string(uint8_t *buf, int32_t len)
{
    std::cout << len << " ";
    for (int i = 0; i < len; i++)
    {
        std::cout << (int)buf[i] << " ";
    }
    std::cout << std::endl;
}

static bool try_one_request(struct Conn *conn)
{

    if (conn->rbuf_size < 4)
    {
        // not enough data in the buffer
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[conn->rbuf_read], 4);

    if (len > k_max_msg)
    {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }

    if (len + 4 > conn->rbuf_size)
    {
        // not enough data .. buffer will retry in the next iteration
        return false;
    }

    assert(conn->rbuf_read + 4 + len < sizeof(conn->rbuf));

    std::cout << "client says: ";
    print_string(&conn->rbuf[conn->rbuf_read], len + 4);

    // Parse the request
    std::vector<std::string> cmd;
    if (0 != parse_req(&conn->rbuf[4 + conn->rbuf_read], len, cmd))
    {
        msg("bad req");
        conn->state = STATE_END;
        return false;
    }

    // Generate a response
    std::string out;
    do_request(cmd, out);

    // Put response into the buffer
    if (4 + out.size() > k_max_msg)
    {
        out.clear();
        out_err(out, ERR_2BIG, "response is too big");
    }

    // Got one request , now generate the response
    uint32_t rescode = 0;
    uint32_t wlen = 0;

    wlen += (uint32_t)out.size();
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], out.data(), out.size());
    conn->wbuf_size = 4 + wlen;

    // generating echoing response
    // memcpy(&conn->wbuf, &len, sizeof(len));
    // memcpy(&conn->wbuf[4], &conn->rbuf[4 + conn->rbuf_read], len);
    // conn->wbuf_size += (4 + len);

    // Move the buffer pointers to begin of next req
    size_t remain = conn->rbuf_size - 4 - len;
    conn->rbuf_read += (4 + len);
    conn->rbuf_size = remain;

    conn->state = STATE_RES;
    state_res(conn);

    return (conn->state == STATE_REQ);
}

static bool cmd_is(const std::string &req, const char *cmd)
{
    return strcasecmp(req.c_str(), cmd) == 0;
}

static int32_t parse_req(const uint8_t *data, uint32_t len, std::vector<std::string> &cmd)
{
    if (len < 4)
    {
        return -1;
    }

    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_args)
    {
        return -1;
    }
    size_t pos = 4;
    while (n--)
    {
        if (pos + 4 > len)
        {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len)
        {
            return -1;
        }
        cmd.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len)
    {
        return -1; // Trailing garbage
    }
    return 0;
}

static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return lhs->hcode == rhs->hcode && le->key == re->key;
}

static void cb_scan(HNode *node, void *arg)
{
    std::string &out = *(std::string *)arg;
    out_str(out, container_of(node, Entry, node)->key);
}

static void do_get(std::vector<std::string> &cmd, std::string &out)
{
    struct Entry key;
    swap(key.key, cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.length());

    HNode *node = g_data.db.hm_lookup(&(key.node), &entry_eq);
    if (!node)
    {
        return out_nil(out);
    }

    std::string &val = container_of(node, Entry, node)->val;
    out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    struct Entry key;
    swap(key.key, cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.length());

    HNode *nd = g_data.db.hm_lookup(&(key.node), &entry_eq);

    if (nd)
    {
        swap(container_of(nd, Entry, node)->val, cmd[2]);
    }
    else
    {
        struct Entry *entry = new Entry();
        swap(entry->key, key.key);
        swap(entry->val, cmd[2]);
        entry->node.hcode = key.node.hcode;

        g_data.db.hm_insert(&(entry->node));
    }

    out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out)
{
    struct Entry key;
    swap(key.key, cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.length());

    HNode *node = g_data.db.hm_pop(&(key.node), &entry_eq);

    if (node)
    {
        delete container_of(node, struct Entry, node);
    }

    out_int(out, node ? 1 : 0);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out)
{
    (void)cmd;
    out_arr(out, (uint32_t)g_data.db.hm_size());
    g_data.db.ht1.h_scan(&cb_scan, &out);
    g_data.db.ht2.h_scan(&cb_scan, &out);
}

static void do_request(std::vector<std::string> &cmd, std::string &out)
{
    if (cmd.size() == 1 && cmd_is(cmd[0], "keys"))
    {
        do_keys(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        do_get(cmd, out);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        do_set(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        do_del(cmd, out);
    }
    else
    {
        // command is not recognised
        out_err(out, ERR_UNKNOWN, "Unknown command");
    }
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
        ;
}

static bool try_flush_buffer(struct Conn *conn)
{
    int32_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    else if (rv < 0)
    {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }

    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size)
    {
        // All of buffer data written , now go back
        conn->wbuf_size = 0;
        conn->wbuf_sent = 0;
        conn->state = STATE_REQ;
        return false;
    }

    // If data remaining in buffer, try to write again
    return true;
}

static void out_nil(std::string &out)
{
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.length();
    out.append((char *)&len, 4);
    out.append(val);
}

static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.length();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0);
        // not expected
    }
}

int main()
{
    int fd;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    int rv = bind(fd, (sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv)
    {
        die("listen()");
    }

    // A map of all client connections keyed by fd
    std::vector<Conn *> fd2conn;

    // Set the listen fd to non-blocking
    fd_set_nb(fd);

    // Event loop
    std::vector<struct pollfd> poll_args;
    while (true)
    {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0}; // Put listening fd in first position
        poll_args.push_back(pfd);

        for (Conn *conn : fd2conn) // Connection fds
        {
            if (!conn)
            {
                continue;
            }

            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // Poll for active fds
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0)
        {
            die("poll");
        }

        //
        for (size_t i = 1; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, fd);
        }
    }

    return 0;
}
