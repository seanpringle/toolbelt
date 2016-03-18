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
  return strchr(field, '.') || strchr(field, ' ') || strchr(field, '(') ? strf("%s", field) : strf("\"%s\"", field);
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

typedef struct {
  db_t *db;
  dbr_t *dbr;
  char *alias;
  char *table;
  map_t *tables;
  vector_t *fields;
  vector_t *where;
  vector_t *order;
  size_t offset;
  size_t limit;
  char *query;
} sql_t;

sql_t*
sql_new (db_t *db)
{
  sql_t *sql = allocate(sizeof(sql_t));
  sql->db = db;
  sql->dbr = NULL;

  sql->alias = NULL;
  sql->table = NULL;
  sql->query = NULL;

  sql->tables = map_new();
  sql->tables->clear = map_clear_free;

  sql->fields = vector_new();
  sql->fields->clear = vector_clear_free;

  sql->where = vector_new();
  sql->where->clear = vector_clear_free;

  sql->order = vector_new();
  sql->order->clear = vector_clear_free;

  return sql;
}

sql_t*
sql_table (sql_t *sql, char *alias, char *table)
{
  sql->alias = strf("%s", alias);
  sql->table = strf("%s", table);
  return sql;
}

sql_t*
sql_join (sql_t *sql, char *alias, char *table)
{
  map_set(sql->tables, strf("%s", alias), strf("%s", table));
  return sql;
}

sql_t*
sql_where_raw (sql_t *sql, char *clause)
{
  vector_push(sql->where, strf("%s", clause));
  return sql;
}

#define sql_wheref(sql,...) ({ sql_t *_s = (sql); vector_push(_s->where, strf(__VA_ARGS__)); _s; })

sql_t*
sql_where (sql_t *sql, char *field, char *op, char *value)
{
  char *f = db_quote_field(sql->db, field);
  char *v = db_quote_value(sql->db, value);
  sql_wheref(sql, "%s %s %s", f, op, v);
  free(f);
  free(v);
  return sql;
}

#define sql_where_eq(sql,field,value) sql_where((sql),(field),"=",(value))
#define sql_where_re(sql,field,value) sql_where((sql),(field),"~",(value))

sql_t*
sql_order (sql_t *sql, char *field, char *direction)
{
  char *f = db_quote_field(sql->db, field);
  vector_push(sql->order, strf("%s %s", f, direction));
  free(f);
  return sql;
}

sql_t*
sql_offset(sql_t *sql, size_t offset)
{
  sql->offset = offset;
  return sql;
}

sql_t*
sql_limit(sql_t *sql, size_t limit)
{
  sql->limit = limit;
  return sql;
}

char*
sql_get_select (sql_t *sql, char *comment)
{
  text_t *tables = textf("%s as %s", sql->table, sql->alias);

  map_each(sql->tables, char *alias, char *name)
    textf_ins(tables, "JOIN %s as %s", alias, name);

  text_t *fields = textf("");

  vector_each(sql->fields, char *field)
    textf_ins(fields, "%s,", field);

  text_trim(fields, iscomma);

  if (!text_count(fields))
    text_set(fields, "*");

  text_t *where = textf("WHERE 1=1");

  vector_each(sql->where, char *clause)
    textf_ins(where, " and %s", clause);

  text_t *order = textf("");

  vector_each(sql->order, char *clause)
    text_ins(order, clause);

  if (text_count(order))
  {
    text_home(order);
    text_ins(order, "ORDER BY ");
  }

  free(sql->query);

  char *offset = sql->offset ? strf("OFFSET %lu", sql->offset): strf("");
  char *limit  = sql->limit  ? strf("LIMIT  %lu", sql->limit) : strf("");

  sql->query = strf("SELECT /* %s */ %s FROM %s %s %s %s",
    comment, text_get(fields), text_get(tables), text_get(where), text_get(order), offset, limit
  );

  text_free(fields);
  text_free(tables);
  text_free(where);
  text_free(order);

  return sql->query;
}

dbr_t*
sql_select (sql_t *sql, char *comment)
{
  dbr_free(sql->dbr);
  sql->dbr = db_query(sql->db, sql_get_select(sql, comment));
  return sql->dbr;
}

size_t
sql_selected (sql_t *sql)
{
  return dbr_selected(sql->dbr);
}

size_t
sql_affected (sql_t *sql)
{
  return dbr_affected(sql->dbr);
}

map_t*
sql_fetch_map (sql_t *sql)
{
  return dbr_fetch_map(sql->dbr);
}

array_t*
sql_fetch_array (sql_t *sql)
{
  return dbr_fetch_array(sql->dbr);
}

void
sql_free (sql_t *sql)
{
  if (sql)
  {
    free(sql->alias);
    free(sql->table);
    free(sql->query);
    map_free(sql->tables);
    vector_free(sql->fields);
    vector_free(sql->where);
    vector_free(sql->order);
    dbr_free(sql->dbr);
    free(sql);
  }
}

#endif
