#pragma once

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class TSignature>
class TCallback;

typedef TCallback<void()> TClosure;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
