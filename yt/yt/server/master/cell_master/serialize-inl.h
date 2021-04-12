#pragma once
#ifndef SERIALIZE_INL_H_
#error "Direct inclusion of this file is not allowed, include serialize.h"
// For the sake of sane code completion.
#include "serialize.h"
#endif

#include <yt/yt/core/misc/serialize.h>
#include <yt/yt/core/misc/mpl.h>

#include <yt/yt/core/yson/string.h>

#include <yt/yt/server/master/object_server/object.h>
#include <yt/yt/server/master/object_server/yson_intern_registry.h>

#include <yt/yt/server/master/cypress_server/node.h>
#include <yt/yt/server/master/cypress_server/serialize.h>

#include <yt/yt/server/master/node_tracker_server/node.h>

namespace NYT::NCellMaster {

////////////////////////////////////////////////////////////////////////////////

struct TNonversionedObjectRefSerializer
{
    static inline const TEntitySerializationKey DestroyedKey = TEntitySerializationKey(-2);

    template <class T>
    static void Save(NCellMaster::TSaveContext& context, T object)
    {
        if (object) {
            // Zombies are serialized as usual, but ghosts need special treatment.
            if (object->IsDestroyed()) {
                // Ephemeral ghosts aren't supposed to be a part of the
                // persistent state. Weak ghosts are.
                YT_VERIFY(object->GetObjectWeakRefCounter() > 0);
                NYT::Save(context, DestroyedKey);
                NYT::Save(context, object->GetId());
            } else {
                auto key = object->GetDynamicData()->SerializationKey;
                YT_ASSERT(key);
                NYT::Save(context, key);
            }
        } else {
            NYT::Save(context, TEntitySerializationKey());
        }
    }

    template <class T>
    static void Save(NCypressServer::TBeginCopyContext& context, T object)
    {
        using NYT::Save;
        if (object && !NObjectServer::IsObjectAlive(object)) {
            THROW_ERROR_EXCEPTION("Object %v is not alive",
                object->GetId());
        }
        Save(context, GetObjectId(object));
    }

    template <class T>
    static void Load(NCellMaster::TLoadContext& context, T& object)
    {
        using TObject = typename std::remove_pointer<T>::type;
        auto key = LoadSuspended<TEntitySerializationKey>(context);
        if (!key) {
            object = nullptr;
            SERIALIZATION_DUMP_WRITE(context, "objref <null>");
        } else if (key == DestroyedKey) {
            auto objectId = LoadSuspended<NObjectServer::TObjectId>(context);
            object = context.GetWeakGhostObject(objectId)->template As<TObject>();
            SERIALIZATION_DUMP_WRITE(context, "objref %v <destroyed>", objectId);
        } else {
            object = context.template GetRawEntity<TObject>(key);
            SERIALIZATION_DUMP_WRITE(context, "objref %v aka %v", object->GetId(), key.Index);
        }
    }

    template <class T>
    static void Load(NCypressServer::TEndCopyContext& context, T& object)
    {
        using NYT::Load;
        using TObject = typename std::remove_pointer<T>::type;
        auto id = Load<NObjectServer::TObjectId>(context);
        object = id
            ? context.template GetObject<TObject>(id)
            : nullptr;
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TVersionedObjectRefSerializer
{
    template <class T, class C>
    static void Save(C& context, T object)
    {
        auto key = object
            ? object->GetDynamicData()->SerializationKey
            : TEntitySerializationKey();
        NYT::Save(context, key);
    }

    template <class T, class C>
    static void Load(C& context, T& object)
    {
        typedef typename std::remove_pointer<T>::type TObject;
        auto key = NYT::Load<TEntitySerializationKey>(context);
        if (key) {
            object = context.template GetRawEntity<TObject>(key);
            SERIALIZATION_DUMP_WRITE(context, "objref %v aka %v", object->GetVersionedId(), key.Index);
        } else {
            object = nullptr;
            SERIALIZATION_DUMP_WRITE(context, "objref <null>");
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TInternedYsonStringSerializer
{
    static inline TEntitySerializationKey UninternedKey = TEntitySerializationKey(-4);

    template <class C>
    static void Save(C& context, const NYson::TYsonString& str)
    {
        using NYT::Save;

        if (!str || str.AsStringBuf().length() < NObjectServer::DefaultYsonStringInternLengthThreshold) {
            Save(context, UninternedKey);
            Save(context, str);
            return;
        }

        auto key = context.RegisterInternedYsonString(str);
        Save(context, key);
        if (key == TEntityStreamSaveContext::InlineKey) {
            Save(context, str);
        }
    }

    template <class C>
    static void Load(C& context, NYson::TYsonString& str)
    {
        using NYT::Load;

        auto key = LoadSuspended<TEntitySerializationKey>(context);
        if (key == UninternedKey) {
            Load(context, str);
        } else if (key == TEntityStreamSaveContext::InlineKey) {
            SERIALIZATION_DUMP_INDENT(context) {
                auto loadedStr = Load<NYson::TYsonString>(context);
                const auto& ysonInternRegistry = context.GetBootstrap()->GetYsonInternRegistry();
                str = ysonInternRegistry->Intern(std::move(loadedStr));
                auto loadedKey = context.RegisterInternedYsonString(str);
                SERIALIZATION_DUMP_WRITE(context, "ysonref %v", loadedKey.Index);
            }
        } else {
            str = context.GetInternedYsonString(key);
            SERIALIZATION_DUMP_WRITE(context, "ysonref %v", key.Index);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMaster

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class T, class C>
struct TSerializerTraits<
    T,
    C,
    typename std::enable_if_t<
        std::conjunction_v<
            std::is_convertible<T, const NObjectServer::TObject*>,
            std::negation<
                std::is_convertible<T, const NCypressServer::TCypressNode*>
            >
        >
    >
>
{
    using TSerializer = NCellMaster::TNonversionedObjectRefSerializer;
    using TComparer = NObjectServer::TObjectRefComparer;
};

template <class T, class C>
struct TSerializerTraits<
    T,
    C,
    typename std::enable_if_t<
        std::is_convertible_v<T, const NCypressServer::TCypressNode*>
    >
>
{
    using TSerializer = NCellMaster::TVersionedObjectRefSerializer;
    using TComparer = NCypressServer::TCypressNodeRefComparer;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

