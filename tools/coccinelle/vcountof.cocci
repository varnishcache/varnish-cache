// Use the macro vcountof when possible
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: https://coccinelle.gitlabpages.inria.fr/website/rules/array.html
// Options: -I ... -all_includes can give more complete results
//
// copied and modified from https://coccinelle.gitlabpages.inria.fr/website/rules/array.cocci

using "varnish.iso"

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(*E))
+ vcountof(E)

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(E[...]))
+ vcountof(E)

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(T))
+ vcountof(E)

