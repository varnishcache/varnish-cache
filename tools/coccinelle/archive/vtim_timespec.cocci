/*
 * This patch refactors to VTIM_timespec().
 */

@@
expression fval, fmod;
struct timespec ts;
type t1, t2;
@@

- ts.tv_nsec = (t1)(modf(fval, &fmod) * 1e9);
- ts.tv_sec = (t2)fmod;
+ ts = VTIM_timespec(fval);

@@
expression fval;
struct timespec ts;
type t1, t2;
@@

- ts.tv_sec = (t1)floor(fval);
- ts.tv_nsec = (t2)(1e9 * (fval - ts.tv_sec));
+ ts = VTIM_timespec(fval);
