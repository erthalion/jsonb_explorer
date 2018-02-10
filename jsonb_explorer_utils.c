#include "postgres.h"

#include <limits.h>

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonb_explorer.h"

static void
add_indent(StringInfo out, bool attach, int level)
{
	int			i;

	appendStringInfoCharMacro(out, '\n');
	for (i = 0; i < level - 1; i++)
		appendBinaryStringInfo(out, "|   ", 4);

	if (attach)
		appendBinaryStringInfo(out, "|---", 4);
	else
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
	int			array_index = 0;
	bool		redo_switch = false;
	bool		indent = true;

	/* If we are indenting, don't add a space after a comma */
	int			ispaces = indent ? 1 : 2;

	/*
	 * Don't indent the very first item. This gets set to the indent flag at
	 * the bottom of the loop.
	 */
	bool		use_indent = false;
	bool		raw_scalar = false;
	bool		last_was_key = false;

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
			add_indent(out, false, level);
			pending_indent = false;
		}

		switch (type)
		{
			case WJB_BEGIN_ARRAY:
				array_index = 1;
				appendStringInfo(out, " [%d elements]", v.val.array.nElems);

				/*if (!first)*/
					/*appendBinaryStringInfo(out, ", ", ispaces);*/

				if (!v.val.array.rawScalar)
				{
					/*add_indent(out, use_indent && !last_was_key, level);*/
					/*appendStringInfoCharMacro(out, '[');*/
				}
				else
					raw_scalar = true;

				first = true;
				pending_indent = true;
				level++;
				break;
			case WJB_BEGIN_OBJECT:
				/*if (!first)*/
					/*appendBinaryStringInfo(out, ", ", ispaces);*/

				if (array_index != 0)
				{
					add_indent(out, use_indent && !last_was_key, level);
					appendStringInfo(out, "# %d", array_index);
				}

				first = true;
				level++;
				break;
			case WJB_KEY:
				if (first)
					add_indent(out, false, level);

				first = true;

				add_indent(out, use_indent, level);

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
				/*ereport(INFO, (errmsg("ELEMENT")));*/
				array_index += 1;

				/*if (!first)*/
					/*appendBinaryStringInfo(out, ", ", ispaces);*/
				first = false;

				/*if (!raw_scalar)*/
					/*add_indent(out, use_indent, level);*/

				break;
			case WJB_END_ARRAY:
				level--;
				array_index = 0;
				if (!raw_scalar)
				{
					/*add_indent(out, use_indent, level);*/
					/*appendStringInfoCharMacro(out, ']');*/
				}
				first = false;
				break;
			case WJB_END_OBJECT:
				level--;
				pending_indent = true;
				first = false;
				break;
			default:
				elog(ERROR, "unknown jsonb iterator token type");
		}
		use_indent = indent;
		last_was_key = redo_switch;
	}

	Assert(level == 0);

	return out->data;
}
