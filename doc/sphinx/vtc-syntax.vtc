# end of paragraph if we hit '*/'
$0 ~ "*/" {
	p = 0;
}

# if in paragraph and no section is announced,
# concatenate
p && $0 !~ "[ /]* SECTION: " {
	cl[section] = cl[section]  gensub(/ \* ?/, "", "1", $0) "\n";
}

# section announcement
$0 ~ "[ /]* SECTION: " {
	section = $3;
	sl[len++] = section;
	tl[section] = gensub(/[\t ]*\/?\* SECTION: [^ ]+ +/, "", "1", $0);
	p = 1;
}

# sort sections, underline titles, print
END {
	asort(sl);
	for (i in sl) {
		section = sl[i]
		print(tl[section]);
		a = section
		c = gsub(/\./, "", a);
		if (c == 0)
			r = "=";
		else if (c == 1)
			r = "*"
		else if (c == 2)
			r = "+"
		else if (c == 3)
			r = "-"
		else
			r = "."
		print(gensub(/./, r, "g", tl[section]));
		print(cl[section]);
	}
}
