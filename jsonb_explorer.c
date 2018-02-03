#include "postgres.h"

#include "catalog/pg_type.h"
#include "utils/jsonb.h"
#include "utils/builtins.h"

#include "jsonb_explorer.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_tree);
Datum jsonb_tree(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_paths);
Datum jsonb_paths(PG_FUNCTION_ARGS);

/*
 * jsonb_tree:
 */
Datum
jsonb_tree(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	StringInfo	str = makeStringInfo();

	JsonbToCStringTree(str, &jb->root, VARSIZE(jb));

	PG_RETURN_TEXT_P(cstring_to_text_with_len(str->data, str->len));
}

/*
 * jsonb_paths:
 */
Datum
jsonb_paths(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	StringInfo	str = makeStringInfo();

	JsonbToCStringIndent(str, &jb->root, VARSIZE(jb));

	PG_RETURN_TEXT_P(cstring_to_text_with_len(str->data, str->len));
}
