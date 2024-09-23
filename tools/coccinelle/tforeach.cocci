/* This patch turns manual loops over a txt into foreach loops */

@@
iterator name Tforeach;
typedef txt;
expression c;
txt t;
@@

- for (c = t.b; c < t.e; c++)
+ Tforeach(c, t)
{
  ...
}
