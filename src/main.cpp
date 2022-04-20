#include "utils.hpp"
#include <boost/program_options.hpp>
#include <cstdio>
#include <filesystem>
#include <httplib.h>
#include <iostream>
#include <libpq-fe.h>
#include <spdlog/spdlog.h>

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

    svr.Post("/hi", [&](const auto &req, auto &ret) {
        auto connection = pool->get_connection();
        auto &test_connection = dynamic_cast<PGConnection &>(*connection);
        auto conn = test_connection.acquire();
        PGresult *res;
        int nFields;
        int i, j;
        /* 检查后端连接成功建立 */
        if (PQstatus(conn) != CONNECTION_OK)
        {
            fprintf(stderr,
                    "Connection to database failed: %s",
                    PQerrorMessage(conn));
            exit_nicely(conn);
        }

        /*
         * 我们的测试实例涉及游标的使用，这个时候我们必须使用事务块。
         * 我们可以把全部事情放在一个  "select * from pg_database"
         * PQexec() 里，不过那样太简单了，不是个好例子。
         */

        /* 开始一个事务块 */
        res = PQexec(conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "BEGIN command failed: %s", PQerrorMessage(conn));
            PQclear(res);
        }

        /*
         * 应该在结果不需要的时候 PQclear PGresult，以避免内存泄漏
         */
        PQclear(res);

        /*
         * 从系统表 pg_database（数据库的系统目录）里抓取数据
         */
        res = PQexec(conn,
                     "DECLARE myportal CURSOR FOR select * from pg_database");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            fprintf(stderr, "DECLARE CURSOR failed: %s", PQerrorMessage(conn));
            PQclear(res);
            exit_nicely(conn);
        }
        PQclear(res);

        res = PQexec(conn, "FETCH ALL in myportal");
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "FETCH ALL failed: %s", PQerrorMessage(conn));
            PQclear(res);
            exit_nicely(conn);
        }

        /* 首先，打印属性名称 */
        nFields = PQnfields(res);
        for (i = 0; i < nFields; i++)
            printf("%-15s", PQfname(res, i));
        printf("\n\n");

        /* 然后打印行 */
        for (i = 0; i < PQntuples(res); i++)
        {
            for (j = 0; j < nFields; j++)
                printf("%-15s", PQgetvalue(res, i, j));
            printf("\n");
        }

        PQclear(res);

        /* 关闭入口 ... 我们不用检查错误 ... */
        res = PQexec(conn, "CLOSE myportal");
        PQclear(res);

        /* 结束事务 */
        res = PQexec(conn, "END");
        pool->release_connection(std::move(connection));
        string content = "{\"key\": \"value\"}";
        ret.set_content(content, "application/json");
    });

    svr.Get("/stop",
            [&](const Request & /*req*/, Response & /*res*/) { svr.stop(); });

    svr.set_error_handler([](const Request & /*req*/, Response &res) {
        const char *fmt =
            "<p>Error Status: <span style='color:red;'>%d</span></p>";
        char buf[BUFSIZ];
        snprintf(buf, sizeof(buf), fmt, res.status);
        res.set_content(buf, "text/html");
    });

    svr.set_logger([](const Request &req, const Response &res) {
        printf("%s", log(req, res).c_str());
    });

    svr.listen(host.c_str(), port);
    return 0;
}
