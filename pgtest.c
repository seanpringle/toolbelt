#include "toolbelt.c"
#include <postgresql/libpq-fe.h>

typedef struct _db_t {
  PGconn *conn;
} db_t;

typedef struct _dbr_t {
  db_t *db;
  PGresult *res;
  size_t rows;
} dbr_t;

void
dbr_clear (dbr_t *dbr)
{
  PQclear(dbr->res);
}

void
dbr_free (dbr_t *dbr)
{
  dbr_clear(dbr);
  free(dbr);
}

size_t
dbr_rows (dbr_t *dbr)
{
  return dbr->rows;
}

dbr_t*
db_write (db_t *db, const char *query)
{
  dbr_t *dbr = allocate(sizeof(dbr_t));
  dbr->res = PQexec(db->conn, query);

  ensure(PQresultStatus(dbr->res) == PGRES_COMMAND_OK)
    errorf("PostgresSQL write failed: %s", PQresultErrorMessage(dbr->res));

  dbr->rows = 0;

  return dbr;
}

dbr_t*
db_read (db_t *db, const char *query)
{
  dbr_t *dbr = allocate(sizeof(dbr_t));
  dbr->res = PQexec(db->conn, query);

  ensure(PQresultStatus(dbr->res) == PGRES_TUPLES_OK)
    errorf("PostgresSQL read failed: %s", PQresultErrorMessage(dbr->res));

  dbr->rows = PQntuples(dbr->res);

  return dbr;
}

void
db_begin (db_t *db)
{
  dbr_free(db_write(db, "begin"));
}

void
db_commit (db_t *db)
{
  dbr_free(db_write(db, "commit"));
}

void
db_rollback (db_t *db)
{
  dbr_free(db_write(db, "rollback"));
}

void
db_command (db_t *db, const char *query)
{
  dbr_free(db_write(db, query));
}

void
db_connect (db_t *db, const char *dbhost, const char *dbname, const char *dbuser, const char *dbpass)
{
  text_t *info = textf("host=%s dbname=%s user=%s password=%s",
    dbhost, dbname, dbuser, dbpass
  );

  db->conn = PQconnectdb(text_get(info));

  ensure(PQstatus(db->conn) == CONNECTION_OK)
    errorf("PostgresSQL connect failed: %s", PQerrorMessage(db->conn));

  text_free(info);
}

void
db_close (db_t *db)
{
  PQfinish(db->conn);
}

int
main (int argc, char *argv[])
{
  db_t _db, *db = &_db;

  db_connect(db, "/var/run/postgresql", "test", "postgres", "");

  db_close(db);

  return 0;
}