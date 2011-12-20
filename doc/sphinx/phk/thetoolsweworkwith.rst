.. _phk_thetoolsweworkwith:

======================
The Tools We Work With
======================

"Only amateurs were limited by their tools" is an old wisdom, and
the world is littered with art and architecture that very much
proves this point.

But as amazing as the Aquaeduct of Segovia is, tools are the reason
why it looks nowhere near as fantastic as the Sydney Opera House.

Concrete has been known since antiquity, but steel-reinforced
concrete and massive numerical calculations of stress-distribution,
is the tools that makes the difference between using concrete as a
filler material between stones, and as gravity-defying curved but
perfectly safe load-bearing wall.

My tool for writing Varnish is the C-language which in many ways
is unique amongst all of the computer programming languages for
having no ambitions.

The C language was invented as a portable assembler language, it
doesn't do objects and garbage-collection, it does numbers and
pointers, just like your CPU.

Compared to the high ambitions, then as now, of new programming
languages, that was almost ridiculous unambitious.  Other people
were trying to make their programming languages provably correct,
or safe for multiprogramming and quite an effort went into using
natural languages as programming languages.

But C was written to write programs, not to research computer science
and that's exactly what made it useful and popular.

Unfortunately C fell in bad company over the years, and the reason
for this outburst is that I just browsed the latest draft from the
ISO-C standardisation working-group 14.

I won't claim that it is enough to make grown men cry, but it
certainly was enough to make me angry.

Let me give you an example of their utter sillyness:

The book which defined the C langauge had a list af reserved
identifiers, all of them lower-case words.  The UNIX libraries
defined a lot of functions, all of them lower-case words.

When compiled, the assembler saw all of these words prefixed
with an underscore, which made it easy to mix assembler and
C code.

All the macros for the C-preprocessor on the other hand, were
UPPERCASE, making them easy to spot.

Which meant that if you mixed upper and lower case, in your
identifiers, you were safe: That wouldn't collide with anything.

First the ISO-C standards people got confused about the leading
underscore, and I'll leave you guessing as to what the current
text actually means:

	All identifiers that begin with an underscore and either
	an uppercase letter or another underscore are always reserved
	for any use.

Feel free to guess, there's more such on pdf page 200 of the draft.

Next, they broke the upper/lower rule, by adding special keywords
in mixed case, probably because they thought it looked nicer::

	_Atomic, _Bool, _Noreturn &c

Then, presumably, somebody pointed out that this looked ugly::

	void _Noreturn foo(int bar);

So they have come up with a #include file called <stdnoreturn.h>
so that instead you can write::

	#include <nostdreturn.h>
	void noreturn foo(int bar);

The <nostdreturn.h> file according to the standard shall have
exactly this content::

	#define noreturn _Noreturn

Are you crying or laughing yet ?   You should be.

Another thing brought by the new draft is an entirely new thread
API, which is incompatible with the POSIX 'pthread' API which have
been used for about 20 years now.

If they had improved on the shortcomings of the pthreads, I would
have cheered them on, because there are some very annoying mistakes
in pthreads.

But they didn't, in fact, as far as I can tell, the C1X draft's
threads are worse than the 20 years older pthreads in all relevant
aspects.

For instance, neither pthreads nor C1X-threads offer a "assert I'm
holding this mutex locked" facility.  I will posit that you cannot
successfully develop real-world threaded programs and APIs without
that, or without wasting a lot of time debugging silly mistakes.

If you look in the Varnish source code, which uses pthreads, you
will see that I have wrapped pthread mutexes in my own little
datastructure, to be able to do those asserts, and to get some
usable statistics on lock-contention.

Another example where C1X did not improve on pthreads at all, was
in timed sleeps, where you say "get me this lock, but give up if
it takes longer than X time".

The way both pthreads and C1X threads do this, is you specify a UTC
wall clock time you want to sleep until.

The only problem with that is that UTC wall clock time is not
continuous when implemented on a computer, and it may not even be
monotonously increasing, since NTPD or other timesync facilites may
step the clock backwards, particularly in the first minutes after
boot.

If the practice of saying "get me this lock before 16:00Z" was
widespread, I could see the point, but I have actually never seen
that in any source code.  What I have seen are wrappers that take
the general shape of::

	int
	get_lock_timed(lock, timeout)
	{
		while (timeout > 0) {
			t0 = time();
			i = get_lock_before(lock, t + timeout));
			if (i == WASLOCKED)
				return (i);
			t1 = time();
			timeout -= (t1 - t0);
		}
		return (TIMEDOUT);
	}

Because it's not like the call is actually guaranteed to return at
16:00Z if you ask it to, you are only promised it will not return
later than that, so you have to wrap the call in a loop.

Whoever defined the select(2) and poll(2) systemcalls knew better
than the POSIX and ISO-C group-think:  They specifed a maximum
duration for the call, because then it doesn't matter what time
it is, only how long time has transpired.

Ohh, and setting the stack-size for a new thread ?
That is appearantly "too dangerous" so there is no argument in the
C1X API for doing so, a clear step backwards from pthreads.

But guess what:  Thread stacks are like T-shirts:  There is no "one
size fits all."

I have no idea what the "danger" they perceived were, my best
guess is that feared it might make the API useful ?

This single idiocy will single-handedly doom the C1X thread API
to uselessness.

Now, don't get me wrong:  There are lot of ways to improve the C
language that would make sense:  Bitmaps, defined structure packing
(think: communication protocol packets), big/little endian variables
(data sharing), sensible handling of linked lists etc.

As ugly as it is, even the printf()/scanf() format strings could
be improved, by offering a sensible plugin mechanism, which the
compiler can understand and use to issue warnings.

Heck, even a simple basic object facility would be good addition,
now that C++ have become this huge bloated monster language.

But none of that is appearantly as important as <stdnoreturn.h>
and a new, crippled and therefore useless thread API.

The neat thing about the C language, and the one feature that made
it so popular, is that not even an ISO-C working group can prevent
you from implementing all these things using macros and other tricks.

But it would be better to have them in the language, so the compiler
could issue sensible warnings and programmers won't have to write
monsters like::

    #define VTAILQ_INSERT_BEFORE(listelm, elm, field) do {              \
        (elm)->field.vtqe_prev = (listelm)->field.vtqe_prev;            \
        VTAILQ_NEXT((elm), field) = (listelm);                          \
        *(listelm)->field.vtqe_prev = (elm);                            \
        (listelm)->field.vtqe_prev = &VTAILQ_NEXT((elm), field);        \
    } while (0)

To put an element on a linked list.

I could go on like this, but it would rapidly become boring for
both you and me, because the current C1X draft is 701 pages, and
it contains not a single explanatory example if how to use any of
the verbiage in practice.

Compare this with The C Programming Language, a book of 274 pages
which in addition to define the C language, taught people how to
program through well-thought-out examples.

From where I sit, ISO WG14 are destroying the C language I use and love.

Poul-Henning, 2011-12-20
