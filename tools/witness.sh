#!/bin/sh

set -e
set -u

readonly work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

witness_full_paths() {
	find "$@" -name '*.log' -print0 |
	xargs -0 awk '$4 == "vsl|" && $6 == "Witness" {
		printf "%s", "ROOT"
		for (i = 8; i <= NF; i++) {
			printf " %s", $i
		}
		printf "\n"
	}' |
	sort |
	uniq
}

witness_edges() {
	awk '{
		for (i = 1; i < NF; i++) {
			printf "%s %s\n", $i, $(i + 1)
		}
	}' |
	sort |
	uniq
}

witness_cycles() {
	! awk -F '[ ,]' '{print $1 " " $(NF - 2)}' |
	tsort >/dev/null 2>&1
}

witness_graph() {
	cat <<-EOF
	digraph {
	    size="8.2,11.7"
	    rankdir="LR"
	    node [fontname="Inconsolata", fontsize="10"]
	    edge [fontname="Inconsolata", fontsize="10"]
	EOF

	awk -F '[ ,]' '{
		printf "    \"%s\" -> \"%s\" [label=\"%s(%s)\"]\n",
		    $1, $(NF - 2), $(NF - 1), $NF
	}' |
	sort |
	uniq

	echo '}'
}

if [ $# -lt 2 ]
then
	cat >&2 <<-EOF
	usage: $0 dot_file test_dirs...
	EOF
	exit 1
fi

dest_file=$1
shift

witness_full_paths "$@" |
witness_edges >"$work_dir/witness-edges.txt"

tsort_err=

if witness_cycles <"$work_dir/witness-edges.txt"
then
	echo "Error: lock cycle witnessed" >&2
	tsort_err=1
fi

witness_graph <"$work_dir/witness-edges.txt" >"$work_dir/witness.dot"

mv "$work_dir/witness.dot" "$dest_file"

exit $tsort_err
