/* stpcpy.c -- copy a string and return pointer to end of new string
   Copyright (C) 1992, 1995, 1997-1998, 2006, 2009-2013 Free Software
   Foundation, Inc.

   NOTE: The canonical source of this file is maintained with the GNU C Library.
   Bugs can be reported to bug-glibc@prep.ai.mit.edu.

   This program is free software: you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#undef __stpcpy
#if defined(_LIBC) || (defined(__MACH__) && defined(stpcpy))
# undef stpcpy
#endif

#ifndef weak_alias
# define __stpcpy stpcpy
#endif

/* Copy SRC to DEST, returning the address of the terminating '\0' in DEST.  */
char *
__stpcpy (char *dest, const char *src)
{
  char *d = dest;
  const char *s = src;

  do
    *d++ = *s;
  while (*s++ != '\0');

  return d - 1;
}
#ifdef weak_alias
weak_alias (__stpcpy, stpcpy)
#endif
