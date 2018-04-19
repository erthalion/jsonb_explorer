#include "postgres.h"

#include <limits.h>

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonb_explorer.h"

#define ARRAY_SIZE 3

#define ARRAY_TYPE 1
#define OBJECT_TYPE 2

typedef struct IndentInfo
{
	int index;
	int length;
	int type;
	int	elem_number;
} IndentInfo;

typedef enum AttachOptions
{
	ATTACH = 0,
	NOT_ATTACH,
	SKIP
} AttachOptions;

static void
add_indent(StringInfo out, AttachOptions attach, int level, IndentInfo *indent_infos)
{
	int	 i;
	IndentInfo last = indent_infos[level];

	appendStringInfoCharMacro(out, '\n');

	for (i = 0; i < level - 1; i++)
	{
		IndentInfo current = indent_infos[i],
				   next = indent_infos[i + 1];
		bool is_array = (current.type == ARRAY_TYPE);
		bool next_is_array = (next.type == ARRAY_TYPE);
		bool next_is_last = (next.index == next.length + 1);

		// don't leave a dangling connection for array items, only for objects
		// inside arrays (it's handled on separate indent levels)
		if (is_array && current.index != 0)
		{
			appendBinaryStringInfo(out, "    ", 4);
			continue;
		}

		// don't leave a dangling connection at the end
		if (next.index != 0 && next_is_last)
		{
			appendBinaryStringInfo(out, "    ", 4);
			continue;
		}

		// don't leave a dangling connection if we'll return to an array and it
		// was the last element
		if (next_is_array && next.index == (next.length - next.elem_number) + 1)
		{
			appendBinaryStringInfo(out, "    ", 4);
			continue;
		}

		appendBinaryStringInfo(out, "│   ", 4);
	}

	// close last item if told so
	if (attach == ATTACH && last.index == last.length + 1)
	{
		appendBinaryStringInfo(out, "└── ", 10);
		return;
	}

	// close last item if it was the last object item in an array
	if (attach == ATTACH && last.type == ARRAY_TYPE && last.index == (last.length - last.elem_number))
	{
		appendBinaryStringInfo(out, "└── ", 10);
		return;
	}

	// leave connection to the next element
	if (attach == ATTACH && last.index != last.length + 1)
	{
		appendBinaryStringInfo(out, "├── ", 10);
		return;
	}

	if (attach == NOT_ATTACH)
	{
		appendBinaryStringInfo(out, "│   ", 4);
		return;
	}
}

// Like a real escape_json_key, but without double quotes
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
	bool				pending_indent = false;
	JsonbIterator	   *it, *forward_it;
	JsonbValue			v, forward_v;
	JsonbIteratorToken	type = WJB_DONE,
					    forward_type = WJB_DONE;
	int					level = 0;			// current nesting level
	int					nested_level = 0;	// current subnesting level inside an array
	int					elem_number = 0;
	int					indent_info_depth = ARRAY_SIZE;
	IndentInfo		   *indent_info = palloc0(indent_info_depth * sizeof(IndentInfo));

	bool				redo_switch = false;
	int					j;

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
			add_indent(out, NOT_ATTACH, level, indent_info);
			pending_indent = false;
		}

		switch (type)
		{
			case WJB_BEGIN_ARRAY:

				// clone the iterator to be able to go in the future,
				// and get some requited information
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

				level++;

				if (level >= indent_info_depth)
				{
					indent_info_depth *= 2;
					indent_info = repalloc(indent_info, indent_info_depth * sizeof(IndentInfo));
					for (j = indent_info_depth / 2; j < indent_info_depth; j++)
					{
						indent_info[j].index = 0;
						indent_info[j].length = 0;
						indent_info[j].type = 0;
						indent_info[j].elem_number = 0;
					}
				}

				indent_info[level].index = 1;
				indent_info[level].length = v.val.array.nElems;
				indent_info[level].type = ARRAY_TYPE;
				indent_info[level].elem_number = elem_number;
				break;
			case WJB_BEGIN_OBJECT:
				if (indent_info[level].index != 0 && indent_info[level].type == ARRAY_TYPE)
				{
					add_indent(out, NOT_ATTACH, level, indent_info);
					add_indent(out, ATTACH, level, indent_info);
					appendStringInfo(out, "# %d", indent_info[level].index);
					indent_info[level].index += 1;
				}

				level++;
				if (level >= indent_info_depth)
				{
					indent_info_depth *= 2;
					indent_info = repalloc(indent_info, indent_info_depth * sizeof(IndentInfo));
					for (j = indent_info_depth / 2; j < indent_info_depth; j++)
					{
						indent_info[j].index = 0;
						indent_info[j].length = 0;
						indent_info[j].type = 0;
						indent_info[j].elem_number = 0;
					}
				}

				indent_info[level].index = 1;
				indent_info[level].length = v.val.object.nPairs;
				indent_info[level].type = OBJECT_TYPE;

				break;
			case WJB_KEY:
				indent_info[level].index += 1;

				add_indent(out, ATTACH, level, indent_info);

				/* json rules guarantee this is a string */
				jsonb_put_escaped_value(out, &v);

				type = JsonbIteratorNext(&it, &v, false);
				if (type != WJB_VALUE)
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
				indent_info[level].index += 1;

				break;
			case WJB_END_ARRAY:
				if (v.val.array.nElems > 0)
					add_indent(out, SKIP, level, indent_info);

				level--;
				break;
			case WJB_END_OBJECT:
				level--;

				if (indent_info[level].index == 0)
					pending_indent = true;

				break;
			default:
				elog(ERROR, "unknown jsonb iterator token type");
		}
	}

	Assert(level == 0);

	return out->data;
}
