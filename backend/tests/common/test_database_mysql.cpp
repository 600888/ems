#include <common/database/mysql_database.h>
#include <gtest/gtest.h>

namespace {
using namespace ems;

TEST(DatabaseMySql, InvalidOptionsAreRejectedWithoutConnecting) {
    database::MySqlConnectionOptions options;
    auto result = database::connect_mysql_database(options, core::immediate_context());
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::InvalidArgument);
    options.user = "test";
    options.database = "test";
    options.max_connections = 0;
    result = database::connect_mysql_database(options, core::immediate_context());
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::InvalidArgument);
}

TEST(DatabaseMySql, ExpiredFactoryDeadlineDoesNotAttemptNetworkIO) {
    database::MySqlConnectionOptions options;
    options.user = "unused";
    options.database = "unused";
    auto result = database::connect_mysql_database(options, core::immediate_context());
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(core::error_of(result).effect, core::WriteEffect::NotApplied);
}

}  // namespace
