#!/bin/sh

# A server side test-script for pushing the ESI XML parser over a
# storage boundary.
# The crucial trick here is that we send these objects HTTP/0.9 style
# so that cache_fetch puts the first 128k in one storage object and
# the rest in another, thus by putting around 128K space in our test
# data we can put it right before, over and after the storage boundary.
#
# Use c.sh as the client side, and run varnish with this vcl:
#
# backend b1 {
#         set backend.host = "Localhost";
#         set backend.port = "8081";
# }
# 
# sub vcl_recv {
# 	pass;
# }
# 
# sub vcl_fetch {
# 	esi;
# }

serve () (
	(
	echo 'HTTP/1.0 200 OK'
	echo ''
	echo "$1"
	dd if=/dev/zero bs=$2 count=1 2>/dev/null | tr '\0' ' ' 
	cat
	sleep .1
	) | nc -l 8081
)



if false ; then
    echo -n "<esi:remove> foo </esi:remove> bar" | serve Test01 1
    echo -n "<esi:remove> foo </esi:remove> bar" | serve Test02 2
    # Unterminated CDATA
    echo -n "<esi:remove> foo </esi:remove> { <![CDATA[foo]] }" | serve Test03 10

    for i in `jot 40 131020`
    do
	echo -n "<esi:remove> foo </esi:remove> bar" | serve Test04::$i $i
    done

    for i in `jot 40 131036`
    do
	echo -n "<!--esi foo --> bar" | serve Test05::$i $i
    done

    for i in `jot 22 131040`
    do
	echo -n "<![CDATA[foo]]>" | serve Test06::$i $i
    done

    echo -n "<esi:remove> " | serve Test07 10

    echo -n "<!--esi " | serve Test08 10

    for i in `jot 10 131042`
    do
	echo -n " > " | serve "Test09:$i <esi:remove" $i
    done

    (
    echo -n "<esi:remove  "
    dd if=/dev/zero bs=32768 count=1 2>/dev/null | tr '\0' ' ' 
    echo -n ">"
    ) | serve "Test10"  131030

    echo -n " ]]> " | serve "Test11:131048 <![CDATA[  " 131048
    echo -n " bar" | serve "Test12 foo <esi:comment comment=\"Humbug!\"/> " 1
    echo -n " bar" | serve "Test13 foo <esi:foo> " 1
fi

while true
do
    echo -n " <esi:say "Hi Mom">" | serve "Test13 foo <esi:foo> " 1
done
