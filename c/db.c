#ifdef TOOLBELT_DB

#include <libpq-fe.h>

typedef struct _db_t {
  PGconn *conn;
} db_t;

typedef struct _dbr_t {
  db_t *db;
  PGresult *res;
  size_t affected;
  size_t selected;
  size_t fetched;
  size_t fields;
  map_t *row_map;
  array_t *row_array;
} dbr_t;

void
dbr_clear (dbr_t *dbr)
{
  PQclear(dbr->res);
}

void
dbr_free (dbr_t *dbr)
{
  if (dbr)
  {
    map_free(dbr->row_map);
    array_free(dbr->row_array);
    dbr_clear(dbr);
    free(dbr);
  }
}

size_t
dbr_affected (dbr_t *dbr)
{
  return dbr ? dbr->affected: 0;
}

size_t
dbr_selected (dbr_t *dbr)
{
  return dbr ? dbr->selected: 0;
}

size_t
dbr_fetched (dbr_t *dbr)
{
  return dbr ? dbr->fetched: 0;
}

array_t*
dbr_fields (dbr_t *dbr)
{
  if (!dbr)
    return NULL;

  array_t *array = array_new(dbr->fields);
  array->clear = array_clear_free;

  for (size_t i = 0; i < dbr->fields; i++)
  {
    char *key = PQfname(dbr->res, i);
    array_set(array, i, strf("%s", key));
  }
  return array;
}

map_t*
dbr_fetch_map (dbr_t *dbr)
{
  if (!dbr)
    return NULL;

  if (dbr->fetched == dbr->selected)
    return NULL;

  map_free(dbr->row_map);
  dbr->row_map = map_new();
  dbr->row_map->clear = map_clear_free;

  for (size_t i = 0; i < dbr->fields; i++)
  {
    char *key = PQfname(dbr->res, i);
    char *val = PQgetisnull(dbr->res, dbr->fetched, i) ? NULL: PQgetvalue(dbr->res, dbr->fetched, i);
    map_set(dbr->row_map, strf("%s", key), val ? strf("%s", val): NULL);
  }
  dbr->fetched++;
  return dbr->row_map;
}

array_t*
dbr_fetch_array (dbr_t *dbr)
{
  if (!dbr)
    return NULL;

  if (dbr->fetched == dbr->selected)
    return NULL;

  array_free(dbr->row_array);
  dbr->row_array = array_new(dbr->fields);
  dbr->row_array->clear = array_clear_free;

  for (size_t i = 0; i < dbr->fields; i++)
  {
    char *val = PQgetisnull(dbr->res, dbr->fetched, i) ? NULL: PQgetvalue(dbr->res, dbr->fetched, i);
    array_set(dbr->row_array, i, val ? strf("%s", val): NULL);
  }
  dbr->fetched++;
  return dbr->row_array;
}

dbr_t*
db_query (db_t *db, const char *query)
{
  dbr_t *dbr = allocate(sizeof(dbr_t));
  dbr->res = PQexec(db->conn, query);

  dbr->row_map   = NULL;
  dbr->row_array = NULL;

  ExecStatusType rs = PQresultStatus(dbr->res);

  if (rs != PGRES_TUPLES_OK && rs != PGRES_SINGLE_TUPLE && rs != PGRES_COMMAND_OK)
  {
    errorf("PostgresSQL query failed: %s: %s", PQresultErrorMessage(dbr->res), query);
    dbr_free(dbr);
    return NULL;
  }

  dbr->affected = strtoll(PQcmdTuples(dbr->res), NULL, 0);
  dbr->selected = PQntuples(dbr->res);
  dbr->fetched  = 0;
  dbr->fields   = PQnfields(dbr->res);
  return dbr;
}

#define db_queryf(db,...) ({ char *_s = strf(__VA_ARGS__); \
  dbr_t *_r = db_query((db), _s); free(_s); _r; })

char*
db_quote_field (db_t *db, const char *field)
{
  return strchr(field, '.') ? strf("%s", field) : strf("\"%s\"", field);
}

char*
db_quote_value (db_t *db, const char *value)
{
  //return str_encode((char*)value, STR_ENCODE_SQL);
  return PQescapeLiteral(db->conn, value, strlen(value));
}

void
db_begin (db_t *db)
{
  dbr_free(db_query(db, "begin"));
}

void
db_commit (db_t *db)
{
  dbr_free(db_query(db, "commit"));
}

void
db_rollback (db_t *db)
{
  dbr_free(db_query(db, "rollback"));
}

void
db_command (db_t *db, const char *query)
{
  dbr_free(db_query(db, query));
}

#define db_commandf(db,...) ({ char *_s = strf(__VA_ARGS__); \
  db_command((db), _s); free(_s); NULL; })

int
db_connect (db_t *db, const char *dbhost, const char *dbname, const char *dbuser, const char *dbpass)
{
  text_t *info = textf("host=%s dbname=%s user=%s password=%s",
    dbhost, dbname, dbuser, dbpass
  );

  db->conn = PQconnectdb(text_get(info));

  text_free(info);

  if (PQstatus(db->conn) != CONNECTION_OK)
  {
    errorf("PostgresSQL connect failed: %s", PQerrorMessage(db->conn));
    PQfinish(db->conn);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

void
db_close (db_t *db)
{
  PQfinish(db->conn);
}

#endif
