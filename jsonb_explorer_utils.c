#include "postgres.h"

#include <limits.h>

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonb_explorer.h"

#define ARRAY_SIZE 3

typedef struct ArrayLevel
{
	int index;
	int nElems;
} ArrayLevel;

typedef enum AttachOptions
{
	ATTACH = 0,
	NOT_ATTACH,
	SKIP
} AttachOptions;

static void
add_indent(StringInfo out, AttachOptions attach, int level, ArrayLevel *array_index)
{
	int	 i;
	char *array_level;

	appendStringInfoCharMacro(out, '\n');
	for (i = 0; i < level - 1; i++)
	{
		if (array_index[i].index != 0 || (array_index[i + 1].index != 0 && array_index[i + 1].index == array_index[i + 1].nElems + 1))
			array_level = "    ";
		else
			array_level = "|   ";
		appendBinaryStringInfo(out, array_level, 4);
	}

	if (attach == ATTACH)
		appendBinaryStringInfo(out, "|---", 4);

	if (attach == NOT_ATTACH)
		appendBinaryStringInfo(out, "|   ", 4);
}

static void
jsonb_put_escaped_value(StringInfo out, JsonbValue *scalarVal)
{
	switch (scalarVal->type)
	{
		case jbvNull:
			appendBinaryStringInfo(out, "null", 4);
			break;
		case jbvString:
			escape_json(out, pnstrdup(scalarVal->val.string.val, scalarVal->val.string.len));
			break;
		case jbvNumeric:
			appendStringInfoString(out,
								   DatumGetCString(DirectFunctionCall1(numeric_out,
																	   PointerGetDatum(scalarVal->val.numeric))));
			break;
		case jbvBool:
			if (scalarVal->val.boolean)
				appendBinaryStringInfo(out, "true", 4);
			else
				appendBinaryStringInfo(out, "false", 5);
			break;
		default:
			elog(ERROR, "unknown jsonb scalar type");
	}
}

char *
JsonbToCStringTree(StringInfo out, JsonbContainer *in, int estimated_len)
{
	bool		first = true, pending_indent = false;
	JsonbIterator *it;
	JsonbValue	v;
	JsonbIteratorToken type = WJB_DONE;
	int			level = 0;
	int			array_index_size = ARRAY_SIZE;
	ArrayLevel	*array_index = palloc0(array_index_size * sizeof(ArrayLevel));

	bool		redo_switch = false;
	int			j;

	if (out == NULL)
		out = makeStringInfo();

	enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

	it = JsonbIteratorInit(in);

	while (redo_switch ||
		   ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE))
	{
		redo_switch = false;
		if (pending_indent)
		{
			add_indent(out, NOT_ATTACH, level, array_index);
			pending_indent = false;
		}

		switch (type)
		{
			case WJB_BEGIN_ARRAY:
				appendStringInfo(out, " [%d elements]", v.val.array.nElems);
				first = true;
				pending_indent = true;
				level++;

				if (level >= array_index_size)
				{
					array_index_size *= 2;
					array_index = repalloc(array_index, array_index_size * sizeof(ArrayLevel));
					for (j = array_index_size / 2; j < array_index_size; j++)
					{
						array_index[j].index = 0;
						array_index[j].nElems = 0;
					}
				}

				array_index[level].index = 1;
				array_index[level].nElems = v.val.array.nElems;
				break;
			case WJB_BEGIN_OBJECT:
				if (array_index[level].index != 0)
				{
					add_indent(out, ATTACH, level, array_index);
					appendStringInfo(out, "# %d", array_index[level].index);
					array_index[level].index += 1;
				}

				first = true;
				level++;
				break;
			case WJB_KEY:
				if (first)
					add_indent(out, NOT_ATTACH, level, array_index);

				first = true;

				add_indent(out, ATTACH, level, array_index);

				/* json rules guarantee this is a string */
				jsonb_put_escaped_value(out, &v);

				type = JsonbIteratorNext(&it, &v, false);
				if (type == WJB_VALUE)
				{
					first = false;
				}
				else
				{
					Assert(type == WJB_BEGIN_OBJECT || type == WJB_BEGIN_ARRAY);

					/*
					 * We need to rerun the current switch() since we need to
					 * output the object which we just got from the iterator
					 * before calling the iterator again.
					 */
					redo_switch = true;
				}
				break;
			case WJB_ELEM:
				array_index[level].index += 1;

				first = false;

				break;
			case WJB_END_ARRAY:
				add_indent(out, SKIP, level, array_index);
				level--;
				first = false;
				break;
			case WJB_END_OBJECT:
				level--;

				if (array_index[level].index == 0)
					pending_indent = true;

				first = false;
				break;
			default:
				elog(ERROR, "unknown jsonb iterator token type");
		}
	}

	Assert(level == 0);

	return out->data;
}
