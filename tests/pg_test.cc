#include <gtest/gtest.h>
#include "../src/utils.hpp"
#include <string>
// Demonstrate some basic assertions.
TEST(AcquiRestPGTest, BasicAssertions)
{
    // Expect equality.
    std::string db_url =
        "user=postgres dbname=postgres password=postgres host=127.0.0.1 "
        "port=5432";
    auto pool = cpool::ConnectionPoolFactory<cpool::PGConnection>::create(
        4, db_url.c_str());
    EXPECT_EQ(pool->size_busy(), 0);
    EXPECT_EQ(pool->size_idle(), 4);
    EXPECT_EQ(pool->size(), 4);

    auto connection = pool->get_connection();
    // auto& test_connection = dynamic_cast< PGConnection& >( *connection );
    // auto conn = test_connection.acquire();
    // EXPECT_EQ(PQstatus(conn), CONNECTION_OK);
    pool->release_connection(std::move(connection));
    EXPECT_EQ(connection.valid(), true);
    EXPECT_EQ(pool->size_busy(), 1);
    EXPECT_EQ(pool->size_idle(), 3);
    EXPECT_EQ(pool->size(), 4);
}
