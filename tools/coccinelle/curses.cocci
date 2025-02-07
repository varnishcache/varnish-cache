
// XXX can we reuse the pattern somehow without inheriting the metavariable?

@@
identifier curs =~ "^(curs_set|(w|mv|mvw|vw_)?printw)$";
expression list EL;
@@
-curs(EL);
+IC(curs(EL));

@@
identifier curs =~ "^(curs_set|(w|mv|mvw|vw_)?printw)$";
expression list EL;
@@
-(void)curs(EL);
+IC(curs(EL));

// ensure we undo AC() patching when we move patterns to IC()
@@
identifier curs =~ "^(curs_set|(w|mv|mvw|vw_)?printw)$";
expression list EL;
@@
-AC(curs(EL));
+IC(curs(EL));

@@
identifier curs =~ "^(endwin|(no)?cbreak|(no)?echo|intrflush|keypad|meta|nodelay|notimeout|(no)?nl|(no)?raw|w?erase|w?clear|w?clrtobot|w?clrtoeol|(w|wnout)?refresh|doupdate|redrawwin|wredrawln|beep|flash|delwin|mv(der)?win|syncok)$";
expression list EL;
@@
-curs(EL);
+AC(curs(EL));

@@
identifier curs =~ "^(endwin|(no)?cbreak|(no)?echo|intrflush|keypad|meta|nodelay|notimeout|(no)?nl|(no)?raw|w?erase|w?clear|w?clrtobot|w?clrtoeol|(w|wnout)?refresh|doupdate|redrawwin|wredrawln|beep|flash|delwin|mv(der)?win|syncok)$";
expression list EL;
@@
-(void)curs(EL);
+AC(curs(EL));

@@
identifier curs =~ "^(endwin|(no)?cbreak|(no)?echo|intrflush|keypad|meta|nodelay|notimeout|(no)?nl|(no)?raw|w?erase|w?clear|w?clrtobot|w?clrtoeol|(w|wnout)?refresh|doupdate|redrawwin|wredrawln|beep|flash|delwin|mv(der)?win|syncok)$";
expression list EL;
@@
-assert(curs(EL) != ERR);
+AC(curs(EL));
