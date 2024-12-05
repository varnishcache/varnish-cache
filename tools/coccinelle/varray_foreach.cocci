@@
identifier i;
expression lim, arr;
iterator name VARRAY_FOREACH;
type T;
@@

T i;
...
- for (i = 0; i < lim; i++)
+ VARRAY_FOREACH(elem, arr, lim)
{
...
- arr[i]
+ *elem
...
}
