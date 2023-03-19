//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_REMOVE_IF_H
#define _LIBCPP___ALGORITHM_REMOVE_IF_H

#include <__algorithm/find_if.h>
#include <__config>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _ForwardIterator, class _Predicate>
_LIBCPP_NODISCARD_EXT _LIBCPP_CONSTEXPR_AFTER_CXX17 _ForwardIterator
remove_if(_ForwardIterator __first, _ForwardIterator __last, _Predicate __pred)
{
    __first = _VSTD::find_if<_ForwardIterator, _Predicate&>(__first, __last, __pred);
    if (__first != __last)
    {
        _ForwardIterator __i = __first;
        while (++__i != __last)
        {
            if (!__pred(*__i))
            {
                *__first = _VSTD::move(*__i);
                ++__first;
            }
        }
    }
    return __first;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_REMOVE_IF_H
