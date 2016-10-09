#pragma once

#include <memory>
#include <vector>

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

struct TFunctionContext
{
    explicit TFunctionContext(std::vector<bool> literalArgs);
    ~TFunctionContext();

    //! Creates typed function-local object.
    //! Function-local objects are destroyed automaticaly when the function context is destroyed.
    //! In case of any error, nullptr is returned.
    template <class T, class... Args>
    T* CreateObject(Args&&... args);

    //! Creates untyped function-local object.
    //! Function-local objects are destroyed automaticaly when the function context is destroyed.
    //! In case of any error, nullptr is returned.
    void* CreateUntypedObject(void* pointer, void(*deleter)(void*));

    void* GetPrivateData() const;

    void SetPrivateData(void* data);

    bool IsLiteralArg(int argIndex) const;

private:
    class TImpl;
    std::unique_ptr<TImpl> Impl_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

#define FUNCTION_CONTEXT_INL_H_
#include "function_context-inl.h"
#undef FUNCTION_CONTEXT_INL_H_

