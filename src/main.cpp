#include "utils.hpp"
#include <boost/program_options.hpp>
#include <cstdio>
#include <filesystem>
#include <httplib.h>
#include <iostream>
#include <libpq-fe.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "argon2.h"

#define OUT_LEN 32
#define ENCODED_LEN 108

using json = nlohmann::json;

using namespace httplib;
using namespace std;
namespace po = boost::program_options;

int main(int argc, const char *argv[])
{
    spdlog::set_pattern("*** [%H:%M:%S %z] [thread %t] [%g] [%!] [%#] %v ***");
    int port;
    string host;
    string database_url;
    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "produce help message")(
        "port,p",
        po::value<int>(&port)->default_value(8080),
        "The port to bind to.")("host,h",
                                po::value<string>(&host)->default_value(
                                    "127.0.0.1"),
                                "The interface to bind to.")(
        "database-url,db",
        po::value<string>(&database_url)
            ->default_value("user=postgres dbname=postgres password=postgres "
                            "host=127.0.0.1 port=5432"),
        "The database config file to use.");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        cout << desc << endl;
        return 0;
    }
    auto pool = cpool::ConnectionPoolFactory<cpool::PGConnection>::create(
        4, database_url.c_str());
    httplib::Server svr;

    if (!svr.is_valid())
    {
        SPDLOG_INFO("server has an error...\n");
        return -1;
    }

    svr.Get(R"(/api/([a-zA-z_]+)/)", [&](const auto &req, auto &res) {
        auto table = req.matches[1];
        res.set_content(table, "text/plain");
    });

    svr.Post("/login/", [&](const auto &req, auto &ret) {
        auto req_json = json::parse(req.body);
        std::string email = req_json["email"];
        if (!is_valid(email))
        {
            // 错误码：参数错误为10 02 XX
            throw std::runtime_error("100201");
        }
        std::string passwd = req_json["password"];
        if (passwd.length() < 6)
        {
            // 错误码：参数错误为10 02 XX
            throw std::runtime_error("100202");
        }
        auto connection = pool->get_connection();
        if (!connection.valid())
        {
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100101");
        }
        auto &test_connection = dynamic_cast<PGConnection &>(*connection);
        auto conn = test_connection.acquire();
        PGresult *res;
        /* 开始一个事务块 */
        res = PQexec(conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "BEGIN command failed: %s", PQerrorMessage(conn));
            PQclear(res);
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100102");
        }
        PQclear(res);
        const char *paramValues[1];
        paramValues[0] = email.c_str();
        res = PQexecParams(conn,
                           "SELECT password, id FROM users WHERE email=$1;",
                           1,    /* one param */
                           NULL, /* let the backend deduce param type */
                           paramValues,
                           NULL, /* don't need param lengths since text */
                           NULL, /* default to all text params */
                           0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "FETCH ALL failed: %s", PQerrorMessage(conn));
            PQclear(res);
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100103");
        }
        if (PQntuples(res) != 1)
        {
            PQclear(res);
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100104");
        }
        auto pass = std::string(PQgetvalue(res, 0, 0));
        auto uid = std::string(PQgetvalue(res, 0, 1));

        if (argon2_verify(pass.c_str(),
                          passwd.c_str(),
                          strlen(passwd.c_str()),
                          Argon2_id) != ARGON2_OK)
        {
            PQclear(res);
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：业务错误为10 03 XX
            throw std::runtime_error("100302");
        }
        PQclear(res);
        auto token = random_string(48);
        const char *paramValues2[2];
        paramValues2[0] = token.c_str();
        paramValues2[1] = uid.c_str();
        res = PQexecParams(conn,
                           "update users set token=$1 where id=$2;",
                           2,    /* one param */
                           NULL, /* let the backend deduce param type */
                           paramValues2,
                           NULL, /* don't need param lengths since text */
                           NULL, /* default to all text params */
                           0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "FETCH ALL failed: %s", PQerrorMessage(conn));
            PQclear(res);
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100103");
        }
        else
        {
            std::cout << "Update counts: " << PQcmdTuples(res) << std::endl;
        }
        /* 结束事务 */
        res = PQexec(conn, "END");
        pool->release_connection(std::move(connection));

        std::string content =
            "{\"code\":\"0\", \"data\": {\"token\": \"";
        content.append(token). append("\"}}");
        ret.set_content(content, "application/json");
    });
    svr.Post("/register/", [&](const auto &req, auto &ret) {
        auto sql = "select * from users where email=$1;";
        auto req_json = json::parse(req.body);
        std::string email = req_json["email"];
        if (!is_valid(email))
        {
            // 错误码：参数错误为10 02 XX
            throw std::runtime_error("100201");
        }
        std::string passwd = req_json["password"];
        if (passwd.length() < 6)
        {
            // 错误码：参数错误为10 02 XX
            throw std::runtime_error("100202");
        }
        auto connection = pool->get_connection();
        if (!connection.valid())
        {
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100101");
        }
        auto &test_connection = dynamic_cast<PGConnection &>(*connection);
        auto conn = test_connection.acquire();
        PGresult *res;
        /* 开始一个事务块 */
        res = PQexec(conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "BEGIN command failed: %s", PQerrorMessage(conn));
            PQclear(res);
            /* 关闭入口 ... 我们不用检查错误 ... */
            res = PQexec(conn, "CLOSE myportal");
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100101");
        }
        PQclear(res);
        const char *paramValues[1];
        paramValues[0] = email.c_str();
        res = PQexecParams(conn,
                           sql,
                           1,    /* one param */
                           NULL, /* let the backend deduce param type */
                           paramValues,
                           NULL, /* don't need param lengths since text */
                           NULL, /* default to all text params */
                           0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "FETCH ALL failed: %s", PQerrorMessage(conn));
            PQclear(res);
            /* 关闭入口 ... 我们不用检查错误 ... */
            res = PQexec(conn, "CLOSE myportal");
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：数据库连接为10 01 XX
            throw std::runtime_error("100101");
        }
        if (PQntuples(res) > 0)
        {
            PQclear(res);
            /* 关闭入口 ... 我们不用检查错误 ... */
            res = PQexec(conn, "CLOSE myportal");
            /* 结束事务 */
            res = PQexec(conn, "END");
            pool->release_connection(std::move(connection));
            // 错误码：业务错误为10 03 XX
            throw std::runtime_error("100303");
        }
        unsigned char out[OUT_LEN];
        unsigned char hex_out[OUT_LEN * 2 + 4];
        char encoded[ENCODED_LEN];
        int hash_ret;
        auto salt = random_string(8);

        SPDLOG_INFO(salt);
        SPDLOG_INFO(passwd);
        hash_ret = argon2_hash(2,
                               1 << 16,
                               1,
                               passwd.c_str(),
                               strlen(passwd.c_str()),
                               salt.c_str(),
                               8,
                               out,
                               OUT_LEN,
                               encoded,
                               ENCODED_LEN,
                               Argon2_id,
                               ARGON2_VERSION_10);
        if (hash_ret == ARGON2_OK)
        {
            SPDLOG_INFO(encoded);
        }
        SPDLOG_INFO(hash_ret);
        PQclear(res);
        /* 关闭入口 ... 我们不用检查错误 ... */
        res = PQexec(conn, "CLOSE myportal");
        /* 结束事务 */
        res = PQexec(conn, "END");
        pool->release_connection(std::move(connection));
        std::string content = std::string(encoded);
        ret.set_content(content, "application/json");
    });

    svr.Get("/stop",
            [&](const Request & /*req*/, Response & /*res*/) { svr.stop(); });

    svr.set_error_handler([](const Request & /*req*/, Response &res) {
        const char *fmt =
            "<p>Error Status: <span style='color:red;'>%d</span></p>";
        char buf[BUFSIZ];
        std::cout << res.status << endl;
        snprintf(buf, sizeof(buf), fmt, res.status);
        res.set_content(buf, "text/html");
    });

    svr.set_exception_handler(
        [](const auto &req, auto &res, std::exception &e) {
            SPDLOG_INFO(e.what());
            res.status = 200;
            res.set_content("{\"k\":\"v\"}", "application/json");
        });
    svr.set_logger([](const Request &req, const Response &res) {
        printf("%s", log(req, res).c_str());
    });

    svr.listen(host.c_str(), port);
    return 0;
}
