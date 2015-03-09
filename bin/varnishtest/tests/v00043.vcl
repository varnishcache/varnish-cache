varnishtest "vcl.state coverage tests"

server s1 {
	rxreq
	txresp
} -start

varnish v1 -vcl+backend {} -start
varnish v1 -vcl+backend {} 

varnish v1 -cliok "param.set vcl_cooldown 5"

varnish v1 -cliok "vcl.list"
varnish v1 -cliok "vcl.use vcl2"
varnish v1 -cliok "vcl.list"
delay 5
varnish v1 -cliok "vcl.list"
varnish v1 -cliok "vcl.state vcl1 warm"
varnish v1 -cliok "vcl.list"
varnish v1 -cliok "vcl.use vcl1"
varnish v1 -cliok "vcl.use vcl2"
delay 5
varnish v1 -cliok "vcl.list"
