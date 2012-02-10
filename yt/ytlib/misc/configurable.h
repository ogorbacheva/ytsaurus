#pragma once

#include "mpl.h"
#include "property.h"

#include <ytlib/ytree/public.h>

namespace NYT {
namespace NConfig {

////////////////////////////////////////////////////////////////////////////////

struct IParameter
    : public TRefCounted
{
    typedef TIntrusivePtr<IParameter> TPtr;

    // node can be NULL
    virtual void Load(const NYTree::INode* node, const NYTree::TYPath& path) = 0;
    virtual void Validate(const NYTree::TYPath& path) const = 0;
    virtual void Save(NYTree::IYsonConsumer* consumer) const = 0;
    virtual bool IsPresent() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TParameter
    : public IParameter
{
public:
    /*!
     * \note Must throw exception for incorrect data
     */
    typedef IParamAction<const T&> TValidator;

    explicit TParameter(T& parameter);

    virtual void Load(const NYTree::INode* node, const NYTree::TYPath& path);
    virtual void Validate(const NYTree::TYPath& path) const;
    virtual void Save(NYTree::IYsonConsumer *consumer) const;
    virtual bool IsPresent() const;

public: // for users
    TParameter& Default(const T& defaultValue = T());
    TParameter& Default(T&& defaultValue);
    TParameter& DefaultNew();
    TParameter& CheckThat(TValidator* validator);
    TParameter& GreaterThan(T value);
    TParameter& GreaterThanOrEqual(T value);
    TParameter& LessThan(T value);
    TParameter& LessThanOrEqual(T value);
    TParameter& InRange(T lowerBound, T upperBound);
    TParameter& NonEmpty();
    
private:
    T& Parameter;
    bool HasDefaultValue;
    yvector<typename TValidator::TPtr> Validators;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NConfig

////////////////////////////////////////////////////////////////////////////////

class TConfigurable
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<TConfigurable> TPtr;

    TConfigurable();

    void LoadAndValidate(const NYTree::INode* node, const NYTree::TYPath& path = "");
    virtual void Load(const NYTree::INode* node, const NYTree::TYPath& path = "");
    void Validate(const NYTree::TYPath& path = "") const;

    void Save(NYTree::IYsonConsumer* consumer) const;

    DEFINE_BYVAL_RW_PROPERTY(bool, KeepOptions);
    NYTree::TMapNodePtr GetOptions() const;

protected:
    virtual void DoValidate() const;

    template <class T>
    NConfig::TParameter<T>& Register(const Stroka& parameterName, T& value);

private:
    template <class T>
    friend class TParameter;

    typedef yhash_map<Stroka, NConfig::IParameter::TPtr> TParameterMap;
    
    TParameterMap Parameters;
    NYTree::TMapNodePtr Options;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define CONFIGURABLE_INL_H_
#include "configurable-inl.h"
#undef CONFIGURABLE_INL_H_
