#include "node_proxy.h"

#include "../ytree/fluent.h"

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

void TAttributeProviderBase::GetAttributeNames(
    TCypressManager::TPtr cypressManager,
    const ICypressNode& node,
    yvector<Stroka>& names)
{
    UNUSED(cypressManager);
    UNUSED(node);
    FOREACH (const auto& name, Names) {
        names.push_back(name);
    }
}

bool TAttributeProviderBase::GetAttribute(
    TCypressManager::TPtr cypressManager,
    const ICypressNode& node,
    const Stroka& name,
    IYsonConsumer* consumer)
{
    auto it = Getters.find(name);
    if (it == Getters.end())
        return false;

    TGetRequest request;
    request.CypressManager = cypressManager;
    request.Node = &node;
    request.Consumer = consumer;

    it->Second()->Do(request);
    return true;
}

void TAttributeProviderBase::RegisterGetter(const Stroka& name, TGetter::TPtr getter)
{
    Names.insert(name);
    YVERIFY(Getters.insert(MakePair(name, getter)).Second());
}

////////////////////////////////////////////////////////////////////////////////

IAttributeProvider* TCypressNodeAttributeProvider::Get()
{
    return Singleton<TCypressNodeAttributeProvider>();
}

TCypressNodeAttributeProvider::TCypressNodeAttributeProvider()
{
    RegisterGetter("id", FromMethod(&TThis::GetId));
}

void TCypressNodeAttributeProvider::GetId(const TGetRequest& request)
{
    TFluentYsonBuilder::Create(request.Consumer)
        .Scalar(request.Node->GetId().NodeId.ToString());
}

////////////////////////////////////////////////////////////////////////////////

TMapNodeProxy::TMapNodeProxy(
    TCypressManager::TPtr cypressManager,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCompositeNodeProxyBase(
        cypressManager,
        transactionId,
        nodeId)
{ }

void TMapNodeProxy::Clear()
{
    EnsureModifiable();
    
    auto& impl = GetTypedImplForUpdate();

    FOREACH(const auto& pair, impl.NameToChild()) {
        auto& childImpl = GetImplForUpdate(pair.Second());
        DetachChild(childImpl);
    }

    impl.NameToChild().clear();
    impl.ChildToName().clear();
}

int TMapNodeProxy::GetChildCount() const
{
    return GetTypedImpl().NameToChild().ysize();
}

yvector< TPair<Stroka, INode::TPtr> > TMapNodeProxy::GetChildren() const
{
    yvector< TPair<Stroka, INode::TPtr> > result;
    const auto& map = GetTypedImpl().NameToChild();
    result.reserve(map.ysize());
    FOREACH (const auto& pair, map) {
        result.push_back(MakePair(
            pair.First(),
            GetProxy<INode>(pair.Second())));
    }
    return result;
}

INode::TPtr TMapNodeProxy::FindChild(const Stroka& name) const
{
    const auto& map = GetTypedImpl().NameToChild();
    auto it = map.find(name);
    return it == map.end() ? NULL : GetProxy<INode>(it->Second());
}

bool TMapNodeProxy::AddChild(INode::TPtr child, const Stroka& name)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();

    auto childProxy = ToProxy(child);
    auto childId = childProxy->GetNodeId();

    if (!impl.NameToChild().insert(MakePair(name, childId)).Second())
        return false;

    auto& childImpl = childProxy->GetImplForUpdate();
    YVERIFY(impl.ChildToName().insert(MakePair(childId, name)).Second());
    AttachChild(childImpl);

    return true;
}

bool TMapNodeProxy::RemoveChild(const Stroka& name)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();

    auto it = impl.NameToChild().find(name);
    if (it == impl.NameToChild().end())
        return false;

    const auto& childId = it->Second();
    auto childProxy = GetProxy<ICypressNodeProxy>(childId);
    auto& childImpl = childProxy->GetImplForUpdate();
    
    impl.NameToChild().erase(it);
    YVERIFY(impl.ChildToName().erase(childId) == 1);

    DetachChild(childImpl);
    
    return true;
}

void TMapNodeProxy::RemoveChild(INode::TPtr child)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();
    
    auto childProxy = ToProxy(child);
    auto& childImpl = childProxy->GetImplForUpdate();

    auto it = impl.ChildToName().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();
    impl.ChildToName().erase(it);
    YVERIFY(impl.NameToChild().erase(name) == 1);

    DetachChild(childImpl);
}

void TMapNodeProxy::ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild)
{
    if (oldChild == newChild)
        return;

    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();

    auto oldChildProxy = ToProxy(oldChild);
    auto& oldChildImpl = oldChildProxy->GetImplForUpdate();
    auto newChildProxy = ToProxy(newChild);
    auto& newChildImpl = newChildProxy->GetImplForUpdate();

    auto it = impl.ChildToName().find(oldChildProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();

    impl.ChildToName().erase(it);
    DetachChild(oldChildImpl);

    impl.NameToChild()[name] = newChildProxy->GetNodeId();
    YVERIFY(impl.ChildToName().insert(MakePair(newChildProxy->GetNodeId(), name)).Second());
    AttachChild(newChildImpl);
}

// TODO: maybe extract base?
IYPathService::TNavigateResult TMapNodeProxy::DoNavigate(TYPath path)
{
    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    auto child = FindChild(prefix);
    if (~child == NULL) {
        throw TYTreeException() << Sprintf("Child %s it not found",
            ~prefix.Quote());
    }

    return TNavigateResult::CreateRecurse(AsYPath(child), tailPath);
}

IYPathService::TSetResult TMapNodeProxy::Set(
    TYPath path,
    TYsonProducer::TPtr producer)
{
    if (path.empty()) {
        SetNodeFromProducer(IMapNode::TPtr(this), producer);
        return TSetResult::CreateDone();
    }

    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    auto child = FindChild(prefix);
    if (~child != NULL) {
        return TSetResult::CreateRecurse(AsYPath(child), tailPath);
    }

    if (tailPath.empty()) {
        TTreeBuilder builder(GetFactory());
        producer->Do(&builder);
        INode::TPtr newChild = builder.GetRoot();
        AddChild(newChild, prefix);
        return TSetResult::CreateDone();
    } else {
        INode::TPtr newChild = ~GetFactory()->CreateMap();
        AddChild(newChild, prefix);
        return TSetResult::CreateRecurse(AsYPath(newChild), tailPath);
    }
}

////////////////////////////////////////////////////////////////////////////////

TListNodeProxy::TListNodeProxy(
    TCypressManager::TPtr cypressManager,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCompositeNodeProxyBase(
        cypressManager,
        transactionId,
        nodeId)
{ }

void TListNodeProxy::Clear()
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();

    FOREACH(auto& nodeId, impl.IndexToChild()) {
        auto& childImpl = GetImplForUpdate(nodeId);
        DetachChild(childImpl);
    }

    impl.IndexToChild().clear();
    impl.ChildToIndex().clear();
}

int TListNodeProxy::GetChildCount() const
{
    return GetTypedImpl().IndexToChild().ysize();
}

yvector<INode::TPtr> TListNodeProxy::GetChildren() const
{
    yvector<INode::TPtr> result;
    const auto& list = GetTypedImpl().IndexToChild();
    result.reserve(list.ysize());
    FOREACH (const auto& nodeId, list) {
        result.push_back(GetProxy<INode>(nodeId));
    }
    return result;
}

INode::TPtr TListNodeProxy::FindChild(int index) const
{
    const auto& list = GetTypedImpl().IndexToChild();
    return index >= 0 && index < list.ysize() ? GetProxy<INode>(list[index]) : NULL;
}

void TListNodeProxy::AddChild(INode::TPtr child, int beforeIndex /*= -1*/)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();

    auto childProxy = ToProxy(child);
    auto childId = childProxy->GetNodeId();
    auto& childImpl = childProxy->GetImplForUpdate();

    if (beforeIndex < 0) {
        YVERIFY(impl.ChildToIndex().insert(MakePair(childId, list.ysize())).Second());
        list.push_back(childId);
    } else {
        YVERIFY(impl.ChildToIndex().insert(MakePair(childId, beforeIndex)).Second());
        list.insert(list.begin() + beforeIndex, childId);
    }

    AttachChild(childImpl);
}

bool TListNodeProxy::RemoveChild(int index)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();

    if (index < 0 || index >= list.ysize())
        return false;

    auto childProxy = GetProxy<ICypressNodeProxy>(list[index]);
    auto& childImpl = childProxy->GetImplForUpdate();

    list.erase(list.begin() + index);
    DetachChild(childImpl);

    return true;
}

void TListNodeProxy::RemoveChild(INode::TPtr child)
{
    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();
    
    auto childProxy = ToProxy(child);
    auto& childImpl = childProxy->GetImplForUpdate();

    auto it = impl.ChildToIndex().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToIndex().end());

    int index = it->Second();
    impl.ChildToIndex().erase(it);
    list.erase(list.begin() + index);
    DetachChild(childImpl);
}

void TListNodeProxy::ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild)
{
    if (oldChild == newChild)
        return;

    EnsureModifiable();

    auto& impl = GetTypedImplForUpdate();

    auto oldChildProxy = ToProxy(oldChild);
    auto& oldChildImpl = oldChildProxy->GetImplForUpdate();
    auto newChildProxy = ToProxy(newChild);
    auto& newChildImpl = newChildProxy->GetImplForUpdate();

    auto it = impl.ChildToIndex().find(oldChildProxy->GetNodeId());
    YASSERT(it != impl.ChildToIndex().end());

    int index = it->Second();

    DetachChild(oldChildImpl);

    impl.IndexToChild()[index] = newChildProxy->GetNodeId();
    YVERIFY(impl.ChildToIndex().insert(MakePair(newChildProxy->GetNodeId(), index)).Second());
    AttachChild(newChildImpl);
}

// TODO: maybe extract base?
IYPathService::TNavigateResult TListNodeProxy::DoNavigate(
    TYPath path)
{
    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    int index;
    try {
        index = FromString<int>(prefix);
    } catch (...) {
        throw TYTreeException() << Sprintf("Failed to parse child index %s",
            ~prefix.Quote());
    }

    return GetYPathChild(index, tailPath);
}

IYPathService::TSetResult TListNodeProxy::Set(
    TYPath path,
    TYsonProducer::TPtr producer)
{
     if (path.empty()) {
        return SetSelf(producer);
    }

    Stroka prefix;
    TYPath tailPath;
    ChopYPathPrefix(path, &prefix, &tailPath);

    if (prefix.empty()) {
        throw TYTreeException() << "Empty child index";
    }

    if (prefix == "+") {
        return CreateYPathChild(GetChildCount(), tailPath, producer);
    } else if (prefix == "-") {
        return CreateYPathChild(0, tailPath, producer);
    }
    
    char lastPrefixCh = prefix[prefix.length() - 1];
    TStringBuf indexString =
        lastPrefixCh == '+' || lastPrefixCh == '-'
        ? TStringBuf(prefix.begin(), prefix.end() - 1)
        : prefix;

    int index;
    try {
        index = FromString<int>(indexString);
    } catch (...) {
        throw TYTreeException() << Sprintf("Failed to parse child index %s",
            ~Stroka(indexString).Quote());
    }

    if (lastPrefixCh == '+') {
        return CreateYPathChild(index + 1, tailPath, producer);
    } else if (lastPrefixCh == '-') {
        return CreateYPathChild(index, tailPath, producer);
    } else {
        auto navigateResult = GetYPathChild(index, tailPath);
        YASSERT(navigateResult.Code == IYPathService::ECode::Recurse);
        return TSetResult::CreateRecurse(navigateResult.RecurseService, navigateResult.RecursePath);
    }
}

IYPathService::TSetResult TListNodeProxy::CreateYPathChild(
    int beforeIndex,
    TYPath tailPath,
    TYsonProducer::TPtr producer)
{
    if (tailPath.empty()) {
        TTreeBuilder builder(GetFactory());
        producer->Do(&builder);
        INode::TPtr newChild = builder.GetRoot();
        AddChild(newChild, beforeIndex);
        return TSetResult::CreateDone();
    } else {
        INode::TPtr newChild = ~GetFactory()->CreateMap();
        AddChild(newChild, beforeIndex);
        return TSetResult::CreateRecurse(AsYPath(newChild), tailPath);
    }
}

IYPathService::TNavigateResult TListNodeProxy::GetYPathChild(
    int index,
    TYPath tailPath) const
{
    int count = GetChildCount();
    if (count == 0) {
        throw TYTreeException() << "List is empty";
    }

    if (index < 0 || index >= count) {
        throw TYTreeException() << Sprintf("Invalid child index %d, expecting value in range 0..%d",
            index,
            count - 1);
    }

    auto child = FindChild(index);
    return TNavigateResult::CreateRecurse(AsYPath(child), tailPath);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

