#pragma once

#include <string>

#include "postgresql/libpq-fe.h"

namespace ercat {

// Class for storing catalog objects, consisting of predefined set of hash maps.
// The object is initialized at the very beginning from the underlying Postgres instance
class PGUtil {
public:
    // helper function for connecting to Postgres with retries
    static PGconn* connectPG(const std::string& pg_conn_str);
    // helper function for executing give Postgres command with retries
    static bool execPGCommand(PGconn** pg_conn, const std::string& pg_conn_str, const std::string& command);
    // helper function for executing give Postgres command once
    static bool execPGCommandOnce(PGconn** pg_conn, const std::string& pg_conn_str, const std::string& command);
};

}