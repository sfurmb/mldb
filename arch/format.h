// This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

/* format.h                                                        -*- C++ -*-
   Jeremy Barnes, 26 February 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
   


   Functions for the manipulation of strings.
*/

#ifndef __arch__format_h__
#define __arch__format_h__

#include <string>
#include "stdarg.h"
#include "mldb/compiler/compiler.h"

namespace ML {

// This machinery allows us to use a std::string with %s via c++11
template<typename T>
JML_ALWAYS_INLINE T forwardForPrintf(T t)
{
    return t;
}

JML_ALWAYS_INLINE const char * forwardForPrintf(const std::string & s)
{
    return s.c_str();
}

std::string formatImpl(const char * fmt, ...) JML_FORMAT_STRING(1, 2);

template<typename... Args>
JML_ALWAYS_INLINE std::string format(const char * fmt, Args... args)
{
    return formatImpl(fmt, forwardForPrintf(args)...);
}

inline std::string format(const char * fmt)
{
    return fmt;
}

std::string vformat(const char * fmt, va_list ap) JML_FORMAT_STRING(1, 0);

} // namespace ML

#endif /* __arch__format_h__ */
