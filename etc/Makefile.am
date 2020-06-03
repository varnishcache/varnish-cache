#

DISTCLEANFILES = builtin.vcl

dist_doc_DATA = builtin.vcl \
		example.vcl

if WITH_LINUX_JAIL
dist_doc_DATA += mgt_filter \
				 vcc_filter \
				 wrk_filter
endif
		

builtin.vcl:	$(top_srcdir)/bin/varnishd/builtin.vcl
	( printf "This is the VCL configuration Varnish will automatically append to your VCL\nfile during compilation/loading. See the vcl(7) man page for details on syntax\nand semantics.\n\
New users is recommended to use the example.vcl file as a starting point.\n\n";\
	sed -n '/vcl_recv/,$$p' $(top_srcdir)/bin/varnishd/builtin.vcl ) | \
	sed 's/^\(.*\)$$/# \1/' > builtin.vcl

vcldir=$(datarootdir)/$(PACKAGE)/vcl

dist_vcl_DATA = devicedetect.vcl
