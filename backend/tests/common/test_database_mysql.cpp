#include <common/database/mysql_database.h>
#include <gtest/gtest.h>
#include <mysql.h>

#include <array>
#include <future>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <limits>
#include <map>
#include <ormpp/mysql.hpp>
#include <thread>

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

TEST(DatabaseMySql, OrmErrorCodesAreIsolatedBetweenPoolThreads) {
    ormpp::mysql main_connection;
    ormpp::mysql::set_last_error("main", 1062);
    std::promise<void> set;
    std::promise<void> checked;
    auto ready = checked.get_future();
    std::thread other([&] {
        ormpp::mysql connection;
        EXPECT_FALSE(connection.has_error());
        ormpp::mysql::set_last_error("other", 1146);
        set.set_value();
        ready.wait();
        EXPECT_EQ(connection.get_last_error_code(), 1146U);
        EXPECT_EQ(connection.get_last_error(), "other");
        ormpp::mysql::reset_error();
    });
    set.get_future().wait();
    EXPECT_EQ(main_connection.get_last_error_code(), 1062U);
    EXPECT_EQ(main_connection.get_last_error(), "main");
    checked.set_value();
    other.join();
    ormpp::mysql::reset_error();
}

TEST(DatabaseMySql, Unsigned64BitBindingPreservesFullVersionRange) {
    ormpp::mysql connection;
    const auto version = std::numeric_limits<std::uint64_t>::max();
    const auto signed_value = std::numeric_limits<std::int64_t>::min();
    std::vector<MYSQL_BIND> parameters;
    connection.set_param_bind(parameters, version);
    connection.set_param_bind(parameters, signed_value);
    ASSERT_EQ(parameters.size(), 2U);
    EXPECT_TRUE(parameters[0].is_unsigned);
    EXPECT_FALSE(parameters[1].is_unsigned);
    EXPECT_EQ(*static_cast<const std::uint64_t*>(parameters[0].buffer), version);

    MYSQL_BIND result{};
    std::uint64_t output = 0;
    decltype(result.is_null) is_null = nullptr;
    std::map<std::size_t, std::vector<char>> buffers;
    connection.set_param_bind(result, output, 0, buffers, is_null);
    EXPECT_TRUE(result.is_unsigned);
    EXPECT_EQ(result.buffer_type, MYSQL_TYPE_LONGLONG);
}
}  // namespace
