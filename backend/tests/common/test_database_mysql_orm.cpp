#include <gtest/gtest.h>
#include <mysql.h>

#include <array>
#include <cstdint>
#include <future>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <limits>
#include <map>
#include <ormpp/mysql.hpp>
#include <thread>

namespace {
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
