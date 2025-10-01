/*
 * Inspect the state returned by ObjWaitState().
 */

@capture_state@
identifier func;
expression oc;
@@

 func(...)
 {
 ...
-ObjWaitState(oc,
+state = ObjWaitState(oc,
 ...);
 ...
-oc->boc->state
+state
 ...
 }

@declare_state@
identifier capture_state.func;
@@

 func(...)
 {
+enum boc_state_e state;
 ...
 }

@ignore_state@
statement stmt;
@@

<...stmt...>
-ObjWaitState(
+(void)ObjWaitState(
 ...);
