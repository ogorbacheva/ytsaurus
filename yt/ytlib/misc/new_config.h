#pragma once

#include "mpl.h"
#include "../actions/action.h"
#include "../actions/action_util.h"
#include "../ytree/ytree.h"

namespace NYT {

class TConfigBase;

namespace NConfig {

////////////////////////////////////////////////////////////////////////////////

struct IParameter
    : public TRefCountedBase
{
    typedef TIntrusivePtr<IParameter> TPtr;

    virtual void Load(NYTree::INode* node, const Stroka& path) = 0;
    virtual void Validate(const Stroka& path) const = 0;
};

////////////////////////////////////////////////////////////////////////////////

template <class T, bool TIsConfig = NYT::NDetail::TIsConvertible<T, TConfigBase>::Value>
class TParameter;

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TParameter<T, true>
    : public IParameter
{
public:
    explicit TParameter(T* parameter);

    virtual void Load(NYTree::INode* node, const Stroka& path);
    virtual void Validate(const Stroka& path) const;

private:
    T* Parameter;
};

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TParameter<T, false>
    : public IParameter
{
public:
    /*!
     * \note Must throw exception for incorrect data
     */
    typedef IParamAction<T> TValidator;

    explicit TParameter(T* parameter);

    virtual void Load(NYTree::INode* node, const Stroka& path);
    virtual void Validate(const Stroka& path) const;

public: // for users
    TParameter& Default(T defaultValue = T());
    TParameter& Check(typename TValidator::TPtr validator);
    TParameter& GreaterThan(T value);
    TParameter& GreaterThanOrEqual(T value);
    TParameter& LessThan(T value);
    TParameter& LessThanOrEqual(T value);
    TParameter& InRange(T lowerBound, T upperBound);
    TParameter& NonEmpty();
    
private:
    T* Parameter;
    bool HasDefaultValue;
    T DefaultValue;
    yvector<typename TValidator::TPtr> Validators;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NConfig

////////////////////////////////////////////////////////////////////////////////

class TConfigBase
{
public:
    virtual ~TConfigBase();
    
    virtual void Load(NYTree::INode* node, const Stroka& path = Stroka());
    virtual void Validate(const Stroka& path = Stroka()) const;

protected:
    template <class T>
    NConfig::TParameter<T>& Register(const Stroka& parameterName, T& value);

private:
    typedef yhash_map<Stroka, NConfig::IParameter::TPtr> ParameterMap;

    ParameterMap Parameters;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define NEW_CONFIG_INL_H_
#include "new_config-inl.h"
#undef NEW_CONFIG_INL_H_
