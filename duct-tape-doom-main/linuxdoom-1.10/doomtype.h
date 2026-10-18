// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Simple basic typedefs, isolated here to make it easier
//	 separating modules.
//    
//-----------------------------------------------------------------------------


#ifndef __DOOMTYPE__
#define __DOOMTYPE__


#ifndef __BYTEBOOL__
#define __BYTEBOOL__
// Fixed to use builtin bool type with C++.
#ifdef __cplusplus
/* C uses an int-sized enum; keep shared engine structs ABI-compatible. */
typedef int boolean;
#else
typedef enum {false, true} boolean;
#endif
typedef unsigned char byte;
#endif


#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* MSVC's C library lacks max_align_t; double has its largest fundamental alignment. */
#if defined(_MSC_VER) && !defined(__clang__)
typedef double max_align_t;
#endif

#define MAXCHAR SCHAR_MAX
#define MAXSHORT INT16_MAX
#define MAXINT INT32_MAX
#define MAXLONG INT32_MAX
#define MINCHAR SCHAR_MIN
#define MINSHORT INT16_MIN
#define MININT INT32_MIN
#define MINLONG INT32_MIN

#ifdef __cplusplus
/* The engine headers are also included by the C++ renderer. */
#define _Noreturn [[noreturn]]
#define _Alignas(type) alignas(type)
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && sizeof(short) == 2,
              "DOOM requires eight-bit bytes, 32-bit int and 16-bit short");
#else
_Static_assert(CHAR_BIT == 8, "DOOM requires eight-bit bytes");
_Static_assert(sizeof(int) == 4, "DOOM requires 32-bit int");
_Static_assert(sizeof(short) == 2, "DOOM requires 16-bit short");
#endif




#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
