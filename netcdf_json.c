/*

Dumps stuff from netcdf to json.

*/

#include "toolbelt.c"
#include <netcdf.h>

int
main (int argc, char *argv[])
{
  int rc = EXIT_SUCCESS;

  char *path = argc > 1 ? argv[1]: str_fgets(stdin);

  str_trim(path, isspace);

  int id = 0;

  if (nc_open(path, NC_NOWRITE, &id) != NC_NOERR)
  {
    errorf("line %d nc_inq %s", __LINE__, path);
    rc = EXIT_FAILURE;
    goto done;
  }

  int num_dimensions         = 0;
  int num_variables          = 0;
  int num_file_attributes    = 0;
  int unlimited_dimension_id = 0;
  int file_format            = 0;

  // <file>
  text_t *json = textf("{");

  if (nc_inq(id, &num_dimensions, &num_variables, &num_file_attributes, &unlimited_dimension_id) != NC_NOERR)
  {
    errorf("line %d nc_inq %s", __LINE__, path);
    rc = EXIT_FAILURE;
    goto done_close;
  }

  if (nc_inq_format(id, &file_format) != NC_NOERR)
  {
    errorf("line %d nc_inq_format %s", __LINE__, path);
    rc = EXIT_FAILURE;
    goto done_close;
  }

  textf_ins(json, "\"format\": \"%s\",",
    (file_format == NC_FORMAT_CLASSIC         ? "NC_FORMAT_CLASSIC":
    (file_format == NC_FORMAT_NETCDF4         ? "NC_FORMAT_NETCDF4":
    (file_format == NC_FORMAT_NETCDF4_CLASSIC ? "NC_FORMAT_NETCDF4_CLASSIC":
      "NC_FORMAT_UNDEFINED")))
  );

  int num_variable_attributes = 0;
  int num_variable_dimensions = 0;
  size_t attribute_length     = 0;
  size_t dimension_length     = 0;

  char variable_name[NC_MAX_NAME];
  char dimension_name[NC_MAX_NAME];
  char attribute_name[NC_MAX_NAME];
  nc_type variable_type, attribute_type;

  // <variables>
  text_ins(json, "\"variables\": {");

  for (int variable_id = 0; variable_id < num_variables; variable_id++)
  {
    if (nc_inq_var(id, variable_id, variable_name, &variable_type, &num_variable_dimensions, NULL, &num_variable_attributes) != NC_NOERR)
    {
      errorf("line %d nc_inq_var %s variable %d %s", __LINE__, path, variable_id, variable_name);
      continue;
    }

    // <variable>
    char *variable_name_quoted = str_encode(variable_name, STR_ENCODE_DQUOTE);
    textf_ins(json, "%s: {", variable_name_quoted);
    free(variable_name_quoted);

    // <type>
    textf_ins(json, "\"type\": \"%s\",",
      (variable_type == NC_BYTE   ? "NC_BYTE":
      (variable_type == NC_UBYTE  ? "NC_UBYTE":
      (variable_type == NC_CHAR   ? "NC_CHAR":
      (variable_type == NC_SHORT  ? "NC_SHORT":
      (variable_type == NC_USHORT ? "NC_USHORT":
      (variable_type == NC_INT    ? "NC_INT":
      (variable_type == NC_UINT   ? "NC_UINT":
      (variable_type == NC_INT64  ? "NC_INT64":
      (variable_type == NC_UINT64 ? "NC_UINT64":
      (variable_type == NC_FLOAT  ? "NC_FLOAT":
      (variable_type == NC_DOUBLE ? "NC_DOUBLE":
        "NC_STRING")))))))))))
    );

    // <dimensions>
    text_ins(json, "\"dimensions\": [");

    int variable_dimensions[num_variable_dimensions];
    size_t dimension_lengths[num_variable_dimensions];

    memset(variable_dimensions,  0, num_variable_dimensions * sizeof(int));
    memset(dimension_lengths, 0, num_variable_dimensions * sizeof(size_t));

    if (nc_inq_var(id, variable_id, NULL, NULL, NULL, variable_dimensions, NULL) != NC_NOERR)
    {
      errorf("line %d nc_inq_var %s variable %d", __LINE__, path, variable_id);
      continue;
    }

    for (int dim = 0; dim < num_variable_dimensions; dim++)
    {
      if (nc_inq_dim(id, variable_dimensions[dim], dimension_name, &dimension_lengths[dim]) != NC_NOERR)
      {
        errorf("line %d nc_inq_dim %s variable %d dimension %d", __LINE__, path, variable_id, variable_dimensions[dim]);
        continue;
      }
      char *dimension_name_quoted = str_encode(dimension_name, STR_ENCODE_DQUOTE);
      textf_ins(json, "%s,", dimension_name_quoted);
      free(dimension_name_quoted);
    }

    // </dimensions>
    text_trim(json, iscomma);
    text_ins(json, " ],");

    // <attributes>
    text_ins(json, "\"attributes\": {");

    for (int attribute_id = 0; attribute_id < num_variable_attributes; attribute_id++)
    {
      if (nc_inq_attname(id, variable_id, attribute_id, attribute_name) != NC_NOERR)
      {
        errorf("line %d nc_inq_attname %s variable %d attribute %d", __LINE__, path, variable_id, attribute_id);
        continue;
      }

      if (nc_inq_att(id, variable_id, attribute_name, &attribute_type, &attribute_length) != NC_NOERR)
      {
        errorf("line %d nc_inq_att %s variable %d attribute %d", __LINE__, path, variable_id, attribute_id);
        continue;
      }

      char *attribute_name_quoted = str_encode(attribute_name, STR_ENCODE_DQUOTE);

      if (attribute_type == NC_CHAR)
      {
        char buffer[attribute_length+1];

        if (nc_get_att(id, variable_id, attribute_name, buffer) != NC_NOERR)
        {
          errorf("line %d nc_get_att %s variable %d attribute %d %s", __LINE__, path, variable_id, attribute_id, attribute_name);
          continue;
        }

        buffer[attribute_length] = 0;

        char *buffer_quoted = str_encode(buffer, STR_ENCODE_DQUOTE);
        textf_ins(json, "%s: %s,", attribute_name_quoted, buffer_quoted);
        free(buffer_quoted);
      }
      else
      if (attribute_type == NC_STRING)
      {
        char *buffer[attribute_length * sizeof(char*)];

        if (nc_get_att_string(id, variable_id, attribute_name, buffer) != NC_NOERR)
        {
          errorf("line %d nc_get_att_string %s variable %d attribute %d %s", __LINE__, path, variable_id, attribute_id, attribute_name);
          continue;
        }

        textf_ins(json, "%s: [", attribute_name_quoted);

        for (int attr = 0; attr < attribute_length; attr++)
        {
          char *s = str_encode(buffer[attr], STR_ENCODE_DQUOTE);
          textf_ins(json, "%s,", s);
          free(s);
        }

        text_trim(json, iscomma);
        text_ins(json, " ]");
      }
      else
      if (
        attribute_type == NC_BYTE   ||
        attribute_type == NC_UBYTE  ||
        attribute_type == NC_SHORT  ||
        attribute_type == NC_USHORT ||
        attribute_type == NC_INT    ||
        attribute_type == NC_UINT   ||
        attribute_type == NC_INT64  ||
        attribute_type == NC_UINT64 ||
        attribute_type == NC_FLOAT  ||
        attribute_type == NC_DOUBLE
      )
      {
        char buffer[32];
        void *ptr =& buffer;

        if (nc_get_att(id, variable_id, attribute_name, buffer) != NC_NOERR)
        {
          errorf("line %d nc_get_att %s variable %d attribute %d %s", __LINE__, path, variable_id, attribute_id, attribute_name);
          continue;
        }

        switch (attribute_type)
        {
          case NC_BYTE:
            textf_ins(json, "%s: %d,", attribute_name_quoted, (int32_t)*((int8_t*)ptr));
            break;

          case NC_UBYTE:
            textf_ins(json, "%s: %u,", attribute_name_quoted, (uint32_t)*((uint8_t*)ptr));
            break;

          case NC_SHORT:
            textf_ins(json, "%s: %d,", attribute_name_quoted, (int32_t)*((int16_t*)ptr));
            break;

          case NC_USHORT:
            textf_ins(json, "%s: %u,", attribute_name_quoted, (uint32_t)*((uint16_t*)ptr));
            break;

          case NC_INT:
            textf_ins(json, "%s: %d,", attribute_name_quoted, *((int32_t*)ptr));
            break;

          case NC_UINT:
            textf_ins(json, "%s: %u,", attribute_name_quoted, *((uint32_t*)ptr));
            break;

          case NC_INT64:
            textf_ins(json, "%s: %ld,", attribute_name_quoted, *((int64_t*)ptr));
            break;

          case NC_UINT64:
            textf_ins(json, "%s: %lu,", attribute_name_quoted, *((uint64_t*)ptr));
            break;

          case NC_FLOAT:
            textf_ins(json, "%s: %10e,", attribute_name_quoted, *((float*)ptr));
            break;

          case NC_DOUBLE:
            textf_ins(json, "%s: %10e,", attribute_name_quoted, *((double*)ptr));
            break;
        }
      }
      free(attribute_name_quoted);
    }

    // </attributes>
    text_trim(json, iscomma);
    text_ins(json, " },");

    // </variable>
    text_trim(json, iscomma);
    text_ins(json, " },");
  }

  // </variables>
  text_trim(json, iscomma);
  text_ins(json, " },");

  // <dimensions>
  text_ins(json, "\"dimensions\": {");

  for (int dimension_id = 0; dimension_id < num_dimensions; dimension_id++)
  {
    if (nc_inq_dim(id, dimension_id, dimension_name, &dimension_length) != NC_NOERR)
    {
      errorf("line %d nc_inq_dim %s dimension %d", __LINE__, path, dimension_id);
      continue;
    }

    char *dimension_name_quoted = str_encode(dimension_name, STR_ENCODE_DQUOTE);

    textf_ins(json, "%s: { \"length\": %lu, \"unlimited\": %s },",
      dimension_name_quoted, dimension_length, unlimited_dimension_id == dimension_id ? "true": "false"
    );

    free(dimension_name_quoted);
  }

  // </dimensions>
  text_trim(json, iscomma);
  text_ins(json, " }");

  // </file>
  text_trim(json, iscomma);
  text_ins(json, " }");

  printf("%s\tnetcdf\t%s", path, text_get(json));
  fflush(stdout);

  text_free(json);

done_close:
  nc_close(id);

done:
  return rc;
}
