/* Declarations for getopt.
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998, 2000
   Free Software Foundation, Inc.

   NOTE: The canonical source of this file is maintained with the GNU C Library.
   Bugs can be reported to bug-glibc@gnu.org.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.  */

#ifndef _GETOPT_H
#define _GETOPT_H 1

/*#include <config.h> */

#if HAVE_UNISTD_H
/* Declares getopt, if present */
#include <unistd.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* We're building this with a C++ compiler, essentially.  Such
   compilers are not required to define __STDC__, but the path we
   should follow, below, is indeed that marked by __STDC__.  We don't
   want to force a definition of __STDC__ (though that works), because
   (a) that feels bad, and (b) some compilers perfectly reasonable
   complain bitterly about it.  So define THIS_IS__STDC__, and replace
   occurrences of __STDC__ throughout with that.

   That means that all of the occurrences of THIS_IS__STDC__ in this
   file and in getopt_long.c are redundant, but I'm leaving them here
   in case it becomes necessary to do cleverer things with it than
   simply define it to be 1, and also as a sort of warped documentation. */
#define THIS_IS__STDC__ 1

#if !HAVE_DECL_GETOPT
/* For communication from `getopt' to the caller.
   When `getopt' finds an option that takes an argument,
   the argument value is returned here.
   Also, when `ordering' is RETURN_IN_ORDER,
   each non-option ARGV-element is returned here.  */

extern char *optarg;

/* Index in ARGV of the next element to be scanned.
   This is used for communication to and from the caller
   and for communication between successive calls to `getopt'.

   On entry to `getopt', zero means this is the first call; initialize.

   When `getopt' returns -1, this is the index of the first of the
   non-option elements that the caller should itself scan.

   Otherwise, `optind' communicates from one call to the next
   how much of ARGV has been scanned so far.  */

extern int optind;

/* Callers store zero here to inhibit the error message `getopt' prints
   for unrecognized options.  */

extern int opterr;

/* Set to an option character which was unrecognized.  */

extern int optopt;

#endif /* ifndef HAVE_DECL_GETOPT */

#if !HAVE_DECL_GETOPT_LONG
/* Describe the long-named options requested by the application.
   The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
   of `struct option' terminated by an element containing a name which is
   zero.

   The field `has_arg' is:
   no_argument		(or 0) if the option does not take an argument,
   required_argument	(or 1) if the option requires an argument,
   optional_argument 	(or 2) if the option takes an optional argument.

   If the field `flag' is not NULL, it points to a variable that is set
   to the value given in the field `val' when the option is found, but
   left unchanged if the option is not found.

   To have a long-named option do something other than set an `int' to
   a compiled-in constant, such as set a value from `optarg', set the
   option's `flag' field to zero and its `val' field to a nonzero
   value (the equivalent single-letter option character, if there is
   one).  For long options that have a zero `flag' field, `getopt'
   returns the contents of the `val' field.  */

struct option
{
#if defined (THIS_IS__STDC__) && THIS_IS__STDC__
  const char *name;
#else
  char *name;
#endif
  /* has_arg can't be an enum because some compilers complain about
     type mismatches in all the code that assumes it is an int.  */
  int has_arg;
  int *flag;
  int val;
};

/* Names for the values of the `has_arg' field of `struct option'.  */

#define	no_argument		0
#define required_argument	1
#define optional_argument	2

#endif /* #if !HAVE_DECL_GETOPT_LONG */

#if defined (THIS_IS__STDC__) && THIS_IS__STDC__
/* HAVE_DECL_* is a three-state macro: undefined, 0 or 1.  If it is
   undefined, we haven't run the autoconf check so provide the
   declaration without arguments.  If it is 0, we checked and failed
   to find the declaration so provide a fully prototyped one.  If it
   is 1, we found it so don't provide any declaration at all.  */
#if defined (__GNU_LIBRARY__) || (defined (HAVE_DECL_GETOPT) && !HAVE_DECL_GETOPT)
/* Many other libraries have conflicting prototypes for getopt, with
   differences in the consts, in stdlib.h.  To avoid compilation
   errors, only prototype getopt for the GNU C library.  */
extern int getopt (int argc, char *const *argv, const char *shortopts);
#else /* not __GNU_LIBRARY__ */
# ifndef _AIX
#  if !defined (HAVE_DECL_GETOPT)
extern int getopt ();
#  endif
# endif
#endif /* __GNU_LIBRARY__ */
#if !HAVE_DECL_GETOPT_LONG
extern int getopt_long (int argc, char *const *argv, const char *shortopts,
		        const struct option *longopts, int *longind);
extern int getopt_long_only (int argc, char *const *argv,
			     const char *shortopts,
		             const struct option *longopts, int *longind);

/* Internal only.  Users should not call this directly.  */
extern int _getopt_internal (int argc, char *const *argv,
			     const char *shortopts,
		             const struct option *longopts, int *longind,
			     int long_only);
#endif /* HAVE_DECL_GETOPT_LONG */
#else /* not THIS_IS__STDC__ */
#if !HAVE_DECL_GETOPT
extern int getopt ();
#endif /* HAVE_DECL_GETOPT */
#if !HAVE_DECL_GETOPT_LONG
extern int getopt_long ();
extern int getopt_long_only ();

extern int _getopt_internal ();
#endif /* HAVE_DECL_GETOPT_LONG */
#endif /* THIS_IS__STDC__ */


#ifdef	__cplusplus
}
#endif

#endif /* getopt.h */