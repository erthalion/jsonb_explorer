#include "postgres.h"

#include <limits.h>

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonb_explorer.h"

#define ARRAY_SIZE 3

#define ARRAY_TYPE 1
#define OBJECT_TYPE 2

typedef struct ArrayLevel
{
	int index;
	int length;
	int type;
	int	elem_number;
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
	ArrayLevel last = array_index[level];

	appendStringInfoCharMacro(out, '\n');
	for (i = 0; i < level - 1; i++)
	{
		char *array_level;
		ArrayLevel current = array_index[i], next = array_index[i + 1];
		bool is_array = current.type == ARRAY_TYPE;
		bool next_is_array = next.type = ARRAY_TYPE;
		bool next_is_last = next.index == next.length + 1;

		if ((is_array && current.index != 0) || (next.index != 0 && next_is_last) || (next_is_array && next.index == (next.length - next.elem_number) + 1))
		/*if ((is_array && current.index != 0) || (next.index != 0 && next_is_last))*/
			array_level = "    ";
		else
			array_level = "│   ";
		appendBinaryStringInfo(out, array_level, 4);
	}

	if (attach == ATTACH && last.index == last.length + 1)
		appendBinaryStringInfo(out, "└── ", 10);


	if (attach == ATTACH && last.type == ARRAY_TYPE && last.index == (last.length - last.elem_number))
		appendBinaryStringInfo(out, "└── ", 10);
	else if (attach == ATTACH && last.index != last.length + 1)
		appendBinaryStringInfo(out, "├── ", 10);

	if (attach == NOT_ATTACH)
		appendBinaryStringInfo(out, "│   ", 4);
}

static void
escape_json_key(StringInfo buf, const char *str)
{
	const char *p;

	for (p = str; *p; p++)
	{
		switch (*p)
		{
			case '\b':
				appendStringInfoString(buf, "\\b");
				break;
			case '\f':
				appendStringInfoString(buf, "\\f");
				break;
			case '\n':
				appendStringInfoString(buf, "\\n");
				break;
			case '\r':
				appendStringInfoString(buf, "\\r");
				break;
			case '\t':
				appendStringInfoString(buf, "\\t");
				break;
			case '"':
				appendStringInfoString(buf, "\\\"");
				break;
			case '\\':
				appendStringInfoString(buf, "\\\\");
				break;
			default:
				if ((unsigned char) *p < ' ')
					appendStringInfo(buf, "\\u%04x", (int) *p);
				else
					appendStringInfoCharMacro(buf, *p);
				break;
		}
	}
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
			escape_json_key(out, pnstrdup(scalarVal->val.string.val, scalarVal->val.string.len));
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
	JsonbIterator *it, *forward_it;
	JsonbValue	v, forward_v;
	JsonbIteratorToken type = WJB_DONE, forward_type = WJB_DONE;
	int			level = 0;
	int			nested_level = 0;
	int			elem_number = 0;
	int			array_index_size = ARRAY_SIZE;
	ArrayLevel	*array_index = palloc0(array_index_size * sizeof(ArrayLevel));

	bool		redo_switch = false;
	int			j;

	if (out == NULL)
		out = makeStringInfo();

	enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

	it = JsonbIteratorInit(in);
	forward_it = palloc(sizeof(JsonbIterator));

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

				memcpy(forward_it, it, sizeof(JsonbIterator));
				forward_it->parent = palloc(sizeof(JsonbIterator));
				memcpy(forward_it->parent, it->parent, sizeof(JsonbIterator));
				forward_v = v;

				elem_number = 0;
				while (!((forward_type = JsonbIteratorNext(&forward_it, &forward_v, false)) == WJB_END_ARRAY && nested_level == 0))
				{
					if (forward_type == WJB_BEGIN_OBJECT || forward_type == WJB_BEGIN_ARRAY)
						nested_level += 1;

					if (forward_type == WJB_END_OBJECT || forward_type == WJB_END_ARRAY)
					{
						nested_level -= 1;
						elem_number = 0;
					}

					if (forward_type == WJB_ELEM && nested_level == 0)
						elem_number += 1;
				}

				appendStringInfo(out, " [%d elements]", v.val.array.nElems);
				first = true;

				/*if (v.val.array.nElems > 0)*/
					/*pending_indent = true;*/

				level++;

				if (level >= array_index_size)
				{
					array_index_size *= 2;
					array_index = repalloc(array_index, array_index_size * sizeof(ArrayLevel));
					for (j = array_index_size / 2; j < array_index_size; j++)
					{
						array_index[j].index = 0;
						array_index[j].length = 0;
						array_index[j].type = 0;
						array_index[j].elem_number = 0;
					}
				}

				array_index[level].index = 1;
				array_index[level].length = v.val.array.nElems;
				array_index[level].type = ARRAY_TYPE;
				array_index[level].elem_number = elem_number;
				break;
			case WJB_BEGIN_OBJECT:
				if (array_index[level].index != 0 && array_index[level].type == ARRAY_TYPE)
				{
					add_indent(out, NOT_ATTACH, level, array_index);
					add_indent(out, ATTACH, level, array_index);
					appendStringInfo(out, "# %d", array_index[level].index);
					array_index[level].index += 1;
				}

				first = true;
				level++;
				if (level >= array_index_size)
				{
					array_index_size *= 2;
					array_index = repalloc(array_index, array_index_size * sizeof(ArrayLevel));
					for (j = array_index_size / 2; j < array_index_size; j++)
					{
						array_index[j].index = 0;
						array_index[j].length = 0;
						array_index[j].type = 0;
						array_index[j].elem_number = 0;
					}
				}

				array_index[level].index = 1;
				array_index[level].length = v.val.object.nPairs;
				array_index[level].type = OBJECT_TYPE;

				break;
			case WJB_KEY:
				array_index[level].index += 1;

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
				if (v.val.array.nElems > 0)
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
