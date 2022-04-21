#pragma once
#include <httplib.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <libpq-fe.h>
#include <cpool/pool.h>
#include <random>
using namespace std;
using namespace httplib;
using namespace cpool;

std::string dump_headers(const Headers &headers)
{
    std::string s;
    char buf[BUFSIZ];

    for (auto it = headers.begin(); it != headers.end(); ++it)
    {
        const auto &x = *it;
        snprintf(
            buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
        s += buf;
    }

    return s;
}

std::string log(const Request &req, const Response &res)
{
    std::string s;
    char buf[BUFSIZ];

    s += "================================\n";

    snprintf(buf,
             sizeof(buf),
             "%s %s %s",
             req.method.c_str(),
             req.version.c_str(),
             req.path.c_str());
    s += buf;

    std::string query;
    for (auto it = req.params.begin(); it != req.params.end(); ++it)
    {
        const auto &x = *it;
        snprintf(buf,
                 sizeof(buf),
                 "%c%s=%s",
                 (it == req.params.begin()) ? '?' : '&',
                 x.first.c_str(),
                 x.second.c_str());
        query += buf;
    }
    snprintf(buf, sizeof(buf), "%s\n", query.c_str());
    s += buf;

    s += dump_headers(req.headers);
    s += (req.body);
    s += "\n";

    s += "--------------------------------\n";

    snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
    s += buf;
    s += dump_headers(res.headers);
    s += "\n";

    if (!res.body.empty())
    {
        s += res.body;
    }

    s += "\n";

    return s;
}
template <class T>
constexpr std::string_view type_name()
{
#ifdef __clang__
    string_view p = __PRETTY_FUNCTION__;
    return string_view(p.data() + 34, p.size() - 34 - 1);
#elif defined(__GNUC__)
    string_view p = __PRETTY_FUNCTION__;
#if __cplusplus < 201402
    return string_view(p.data() + 36, p.size() - 36 - 1);
#else
    return string_view(p.data() + 49, p.find(';', 49) - 49);
#endif
#elif defined(_MSC_VER)
    string_view p = __FUNCSIG__;
    return string_view(p.data() + 84, p.size() - 84 - 7);
#endif
}

namespace cpool
{
class PGConnection final : public Connection
{
  public:
    bool heart_beat() override
    {
        return connected;
    }
    bool is_healthy() override
    {
        return connected;
    }
    bool connect() override
    {
        conn = PQconnectdb(conninfo);
        /* 检查后端连接成功建立 */
        if (PQstatus(conn) != CONNECTION_OK)
        {
            fprintf(stderr,
                    "Connection to database failed: %s",
                    PQerrorMessage(conn));
            return false;
        }
        connected = true;
        return connected;
    }
    void disconnect() override
    {
        PQfinish(conn);
        connected = false;
    }
    PGconn* acquire()
    {
        return conn;
    }

  private:
    PGconn *conn;
    const char *conninfo =
        "user=postgres dbname=postgres password=postgres host=127.0.0.1 "
        "port=5432";
    PGConnection() = default;
    PGConnection(const char *db_url) : conninfo(db_url)
    {
    }
    friend ConnectionPoolFactory<PGConnection>;
    bool connected = false;
};
template <>
class ConnectionPoolFactory<PGConnection>
{
  public:
    static std::unique_ptr<ConnectionPool> create(
        const std::uint16_t num_connections,
        const char *db_url)
    {
        std::vector<std::unique_ptr<Connection>> connections;
        for (std::uint16_t k = 0; k < num_connections; ++k)
        {
            // cannot use std::make_unique, because constructor is hidden
            connections.emplace_back(
                std::unique_ptr<PGConnection>(new PGConnection(db_url)));
        }
        return std::unique_ptr<ConnectionPool>(
            new ConnectionPool{std::move(connections)});
    }
};

}  // namespace cpool

static void
show_binary_results(PGresult *res)
{
    int         i,
                j;
    int         i_fnum,
                t_fnum,
                b_fnum;

    /* Use PQfnumber to avoid assumptions about field order in result */
    i_fnum = PQfnumber(res, "i");
    t_fnum = PQfnumber(res, "t");
    b_fnum = PQfnumber(res, "b");

    for (i = 0; i < PQntuples(res); i++)
    {
        char       *iptr;
        char       *tptr;
        char       *bptr;
        int         blen;
        int         ival;

        /* Get the field values (we ignore possibility they are null!) */
        iptr = PQgetvalue(res, i, i_fnum);
        tptr = PQgetvalue(res, i, t_fnum);
        bptr = PQgetvalue(res, i, b_fnum);

        /*
         * The binary representation of INT4 is in network byte order, which
         * we'd better coerce to the local byte order.
         */
        ival = ntohl(*((uint32_t *) iptr));

        /*
         * The binary representation of TEXT is, well, text, and since libpq
         * was nice enough to append a zero byte to it, it'll work just fine
         * as a C string.
         *
         * The binary representation of BYTEA is a bunch of bytes, which could
         * include embedded nulls so we have to pay attention to field length.
         */
        blen = PQgetlength(res, i, b_fnum);

        printf("tuple %d: got\n", i);
        printf(" i = (%d bytes) %d\n",
               PQgetlength(res, i, i_fnum), ival);
        printf(" t = (%d bytes) '%s'\n",
               PQgetlength(res, i, t_fnum), tptr);
        printf(" b = (%d bytes) ", blen);
        for (j = 0; j < blen; j++)
            printf("\\%03o", bptr[j]);
        printf("\n\n");
    }
}

bool is_valid(const string& email)
{

    // Regular expression definition
    const regex pattern(
        "(\\w+)(\\.|_)?(\\w*)@(\\w+)(\\.(\\w+))+");

    // Match the string pattern
    // with regular expression
    return regex_match(email, pattern);
}

std::string random_string(int max_length=32)
{
     std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

     std::random_device rd;
     std::mt19937 generator(rd());

     std::shuffle(str.begin(), str.end(), generator);

     return str.substr(0, max_length);    // assumes 32 < number of characters in str
}
