#include <iostream>

#include "utils/pgutil.h"

namespace ercat{

PGconn* PGUtil::connectPG(const std::string& pg_conn_str) {
    int retry = 0;
    PGconn* pg_conn = PQconnectdb(pg_conn_str.c_str());
    while (PQstatus(pg_conn) != CONNECTION_OK && retry < 5) {
        std::cerr << "Error: Catalog connection failure. Retrying...\n";
        PQfinish(pg_conn);
        pg_conn = PQconnectdb(pg_conn_str.c_str());
        retry++;
    }
    return pg_conn;
}

bool PGUtil::execPGCommand(PGconn** pg_conn, const std::string& pg_conn_str, const std::string& command) {
    PGresult *res;
    int retry = 0;
    bool success = false;
    while (!success && retry < 5) {
        if (!PQsendQuery(*pg_conn, command.c_str())) {
            std::cerr << PQerrorMessage(*pg_conn) << "\n";
            if (PQstatus(*pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(*pg_conn);
                *pg_conn = PQconnectdb(pg_conn_str.c_str());
            }
            retry++;
        }
        else {
            success = true;
            while ((res = PQgetResult(*pg_conn)) != NULL) {
                success = success && (PQresultStatus(res) == PGRES_COMMAND_OK 
                        || PQresultStatus(res) == PGRES_TUPLES_OK);
                // TODO take this out once fully debugged         
                if (PQresultStatus(res) != PGRES_COMMAND_OK && 
                        PQresultStatus(res) != PGRES_TUPLES_OK) {
                    std::cerr <<  PQresultErrorMessage(res) << "\n";
                    PQclear(res);

                    while ((res = PQgetResult(*pg_conn)) != NULL) {
                        PQclear(res);
                    }

                    // 3. Send a ROLLBACK to clear the aborted transaction state on the server
                    res = PQexec(*pg_conn, "ROLLBACK");
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        std::cerr << "Rollback failed: " << PQresultErrorMessage(res) << "\n";
                        PQclear(res);
                        
                        PQreset(*pg_conn);
                    }
                    else {
                        PQclear(res);
                    }
                }
                else {
                    PQclear(res);
                }
            }
        }
    }
    return success;
}

bool PGUtil::execPGCommandOnce(PGconn** pg_conn, const std::string& pg_conn_str, const std::string& command) {
    PGresult *res;
    int retry = 0;
    bool success = false;
    while (!success && retry < 5) {
        if (!PQsendQuery(*pg_conn, command.c_str())) {
            std::cerr << PQerrorMessage(*pg_conn) << "\n";
            if (PQstatus(*pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(*pg_conn);
                *pg_conn = PQconnectdb(pg_conn_str.c_str());
            }
            retry++;
        }
        else {
            success = true;
            while ((res = PQgetResult(*pg_conn)) != NULL) {
                success = success && (PQresultStatus(res) == PGRES_COMMAND_OK 
                        || PQresultStatus(res) == PGRES_TUPLES_OK);
                // TODO take this out once fully debugged         
                if (PQresultStatus(res) != PGRES_COMMAND_OK && 
                        PQresultStatus(res) != PGRES_TUPLES_OK) {
                    std::cerr <<  PQresultErrorMessage(res) << "\n";
                    PQclear(res);
                    
                    while ((res = PQgetResult(*pg_conn)) != NULL) {
                        PQclear(res);
                    }

                    // 3. Send a ROLLBACK to clear the aborted transaction state on the server
                    res = PQexec(*pg_conn, "ROLLBACK");
                    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                        std::cerr << "Rollback failed: " << PQresultErrorMessage(res) << "\n";
                        PQclear(res);
                        
                        PQreset(*pg_conn);
                    }
                    else {
                        PQclear(res);
                    }
                }
                else {
                    PQclear(res);
                }
                
            }
            break;
        }
    }
    return success;
}

}

