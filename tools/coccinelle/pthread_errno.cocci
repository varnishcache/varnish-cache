/*
 * This patch ensures that we use the new PTOK macro instead of AZ(pthread_*(...)).
 */

@@
expression list ARGS;
@@

- AZ(pthread_attr_destroy(ARGS))
+ PTOK(pthread_attr_destroy(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_attr_getstacksize(ARGS))
+ PTOK(pthread_attr_getstacksize(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_attr_init(ARGS))
+ PTOK(pthread_attr_init(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_attr_setdetachstate(ARGS))
+ PTOK(pthread_attr_setdetachstate(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_attr_setstacksize(ARGS))
+ PTOK(pthread_attr_setstacksize(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_cond_broadcast(ARGS))
+ PTOK(pthread_cond_broadcast(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_cond_destroy(ARGS))
+ PTOK(pthread_cond_destroy(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_cond_init(ARGS))
+ PTOK(pthread_cond_init(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_cond_signal(ARGS))
+ PTOK(pthread_cond_signal(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_cond_wait(ARGS))
+ PTOK(pthread_cond_wait(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_create(ARGS))
+ PTOK(pthread_create(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_detach(ARGS))
+ PTOK(pthread_detach(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_join(ARGS))
+ PTOK(pthread_join(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_key_create(ARGS))
+ PTOK(pthread_key_create(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_kill(ARGS))
+ PTOK(pthread_kill(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutexattr_init(ARGS))
+ PTOK(pthread_mutexattr_init(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutexattr_settype(ARGS))
+ PTOK(pthread_mutexattr_settype(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutex_destroy(ARGS))
+ PTOK(pthread_mutex_destroy(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutex_init(ARGS))
+ PTOK(pthread_mutex_init(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutex_lock(ARGS))
+ PTOK(pthread_mutex_lock(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_mutex_unlock(ARGS))
+ PTOK(pthread_mutex_unlock(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_once(ARGS))
+ PTOK(pthread_once(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_rwlock_destroy(ARGS))
+ PTOK(pthread_rwlock_destroy(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_rwlock_init(ARGS))
+ PTOK(pthread_rwlock_init(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_rwlock_rdlock(ARGS))
+ PTOK(pthread_rwlock_rdlock(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_rwlock_unlock(ARGS))
+ PTOK(pthread_rwlock_unlock(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_rwlock_wrlock(ARGS))
+ PTOK(pthread_rwlock_wrlock(ARGS))

@@
expression list ARGS;
@@

- AZ(pthread_setspecific(ARGS))
+ PTOK(pthread_setspecific(ARGS))
