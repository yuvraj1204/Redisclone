#include <iostream>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <assert.h>
#include <fcntl.h>
#include <vector>

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

const size_t k_max_msg = 4096;

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
        ssize_t rv = read(fd, buf, n); // Read exactly n bytes into buf
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

// static int32_t send_req(int fd, const char *text)
// {
//     uint32_t len = (uint32_t)strlen(text);
//     if (len > k_max_msg)
//     {
//         return -1;
//     }

//     char wbuf[4 + k_max_msg];
//     memcpy(wbuf, &len, 4); // assume little endian
//     memcpy(wbuf + 4, text, len);

//     if (int32_t err = write_all(fd, wbuf, 4 + len))
//     {
//         return err;
//     }
//     return 0;
// }

static int32_t send_req(int fd, std::vector<std::string> &cmd)
{
    uint32_t len = 4;
    for (const std::string &s : cmd)
    {
        len += 4 + s.size();
    }
    if (len > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4);
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

// static int32_t read_res(int fd, const char *text)
// {
//     // 4 bytes header
//     char rbuf[4 + k_max_msg + 1];
//     errno = 0;
//     int32_t err = read_full(fd, rbuf, 4);
//     if (err)
//     {
//         if (errno == 0)
//         {
//             msg("EOF");
//         }
//         else
//         {
//             msg("read() error");
//         }
//         return err;
//     }

//     uint32_t len = 0;
//     memcpy(&len, rbuf, 4);
//     if (len > k_max_msg)
//     {
//         msg("too long");
//         return -1;
//     }

//     // reply body
//     err = read_full(fd, rbuf + 4, len);
//     if (err)
//     {
//         msg("read() error");
//         return err;
//     }

//     // do something
//     rbuf[4 + len] = '\0';
//     std::cout << "server says: " << rbuf + 4 << std::endl;

//     return 0;
// }

static int32_t on_response(const uint8_t *data, size_t size)
{
    if (size < 1)
    {
        msg("bad response");
        return -1;
    }
    switch (data[0])
    {

    case SER_NIL:
        std::cout << "(nil)" << std::endl;
        return 1;

    case SER_ERR:
        if (size < 1 + 8)
        {
            msg("bad response");
            return -1;
        }
        else
        {
            int32_t code = 0;
            uint32_t len = 0;
            memcpy(&code, &data[1], 4);
            memcpy(&len, &data[1 + 4], 4);

            if (size < 1 + 8 + len)
            {
                msg("bad response");
                return -1;
            }
            std::cout << "(err) ";
            std::cout.write((char *)&data[1 + 8], len);
            std::cout << std::endl;
            return 1 + 8 + len;
        }

    case SER_STR:
        if (size < 1 + 4)
        {
            msg("bad response");
            return -1;
        }
        else
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);

            if (size < 1 + 4 + len)
            {
                msg("bad response");
                return -1;
            }
            std::cout << "(str) ";
            std::cout.write((char *)&data[1 + 4], len);
            std::cout << std::endl;
            return 1 + 4 + len;
        }

    case SER_INT:
        if (size < 1 + 8)
        {
            msg("bad response");
            return -1;
        }
        else
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8);
            std::cout << "(int) " << val << std::endl;
            return 1 + 8;
        }

    case SER_ARR:
        if (size < 1 + 4)
        {
            msg("bad response");
            return -1;
        }
        else
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            std::cout << "(arr) len=" << len << std::endl;
            size_t arr_bytes = 1 + 4;
            for (u_int32_t i = 0; i < len; i++)
            {
                int32_t rv = on_response(&data[arr_bytes], size - arr_bytes);
                if (rv < 0)
                {
                    return rv;
                }
                arr_bytes += (size_t)rv;
            }
            std::cout << "(arr) end" << std::endl;
            return (int32_t)arr_bytes;
        }

    default:
        msg("bad response");
        return -1;
    }
}

static int32_t read_res(int fd, std::vector<std::string> &cmd)
{
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
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
    memcpy(&len, rbuf, 4); // assume little endian
    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, &rbuf[4], len);
    if (err)
    {
        msg("read() error");
        return err;
    }

    // print the result
    // uint32_t rescode = 0;
    // if (len < 4)
    // {
    //     msg("bad response");
    //     return -1;
    // }
    // memcpy(&rescode, &rbuf[4], 4);
    // // std::cout << ("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
    // std::cout << "Server says: [" << rescode << "] ";
    // fprintf(stdout, "%.*s", len - 4, &rbuf[8]);
    // std::cout << std::endl;
    // return 0;

    // print the result
    int32_t rv = on_response((uint8_t *)&rbuf[4], len);
    if (rv > 0 && (uint32_t)rv != len)
    {
        msg("bad response");
        rv = -1;
    }
    return rv;
}

int main(int argc, char **argv)
{
    int fd;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    int rv = connect(fd, (const sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("connect()");
    }

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; i++)
    {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(fd, cmd);
    if (err)
    {
        goto L_DONE;
    }
    err = read_res(fd, cmd);
    if (err)
    {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}
