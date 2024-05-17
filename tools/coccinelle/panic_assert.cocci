/*
 * This patch replaces a non-exhaustive list of statements designed to trigger
 * assertions with something more appropriate for panic code.
 *
 * The heuristics for panic functions include:
 * - something panic-related in the name
 * - taking a VSB
 * - returning void
 */

@panic@
identifier sb, func =~ "(^PAN_|^pan_|_panic$)";
@@

void func(..., struct vsb *sb, ...) { ... }

@@
identifier panic.sb, panic.func;
expression obj, magicval;
@@

func(...)
{
...
- CHECK_OBJ_NOTNULL(obj, magicval);
+ PAN_CheckMagic(sb, obj, magicval);
...
}

@@
identifier panic.sb, panic.func;
expression obj, from, magicval;
@@

func(...)
{
...
- CAST_OBJ_NOTNULL(obj, from, magicval);
+ obj = from;
+ PAN_CheckMagic(sb, obj, magicval);
...
}
