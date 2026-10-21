#include <common/database/mysql_database.h>

namespace ems::database {

core::Result<MySqlDatabaseContext> connect_mysql_database(const MySqlConnectionOptions&,
                                                          const core::CallContext&) {
    return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplied,
                            "MySQL backend is not built");
}

}  // namespace ems::database
