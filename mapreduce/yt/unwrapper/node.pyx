from util.generic.vector cimport TVector
from util.generic.hash cimport THashMap
from util.generic.string cimport TString
from util.system.types cimport i64, ui64


cdef extern from "library/yson/node/node.h" namespace "NYT" nogil:
    cdef cppclass TNode:
        TNode() except +
        TNode(const char*) except +
        TNode(TString) except +
        TNode(double) except +
        TNode(bint) except +
        TNode(i64) except +
        TNode(ui64) except +

        bint IsString()
        bint IsInt64()
        bint IsUint64()
        bint IsDouble()
        bint IsBool()
        bint IsList()
        bint IsMap()
        bint IsEntity()
        bint IsUndefined()

        TString& AsString()
        i64 AsInt64()
        ui64 AsUint64()
        double AsDouble()
        bint AsBool()
        TVector[TNode]& AsList()
        THashMap[TString, TNode]& AsMap()

        @staticmethod
        TNode CreateList()
        @staticmethod
        TNode CreateMap()

        TNode operator()(TString, TNode)
        TNode Add(TNode)


class Node(object):
    INT64 = 0
    UINT64 = 1
    _ALL_TYPES = {INT64, UINT64}

    def __init__(self, data, node_type):
        self.data = data
        if node_type not in Node._ALL_TYPES:
            raise Exception('unsupported node_type')
        self.node_type = node_type


def node_i64(i):
    return Node(i, Node.INT64)


def node_ui64(ui):
    return Node(ui, Node.UINT64)


cdef TString _to_TString(s):
    assert isinstance(s, (basestring, bytes))
    if isinstance(s, unicode):
        s = s.encode('UTF-8')
    return TString(<const char*>s, len(s))


cdef _TNode_to_pyobj(TNode node):
    if node.IsString():
        return node.AsString()
    elif node.IsInt64():
        return node.AsInt64()
    elif node.IsUint64():
        return node.AsUint64()
    elif node.IsDouble():
        return node.AsDouble()
    elif node.IsBool():
        return node.AsBool()
    elif node.IsEntity():
        return None
    elif node.IsUndefined():
        return None
    elif node.IsList():
        node_list = node.AsList()
        return [_TNode_to_pyobj(n) for n in node_list]
    elif node.IsMap():
        node_map = node.AsMap()
        return {p.first: _TNode_to_pyobj(p.second) for p in node_map}
    else:
        # should never happen
        raise Exception()


cdef TNode _pyobj_to_TNode(obj):
    if isinstance(obj, Node):
        if obj.node_type == Node.INT64:
            return TNode(<i64>obj.data)
        elif obj.node_type == Node.UINT64:
            return TNode(<ui64>obj.data)
        else:
            # should never happen
            raise Exception()
    elif isinstance(obj, (basestring, bytes)):
        return TNode(_to_TString(obj))
    elif isinstance(obj, long):
        if obj < 2**63:
            return TNode(<i64>obj)
        else:
            return TNode(<ui64>obj)
    elif isinstance(obj, bool):
        return TNode(<bint>obj)
    elif isinstance(obj, int):
        return TNode(<i64>obj)
    elif isinstance(obj, float):
        return TNode(<float>obj)
    elif isinstance(obj, dict):
        node = TNode.CreateMap()
        for k, v in obj.iteritems():
            node(_to_TString(k), _pyobj_to_TNode(v))
        return node
    elif isinstance(obj, list):
        node = TNode.CreateList()
        for x in obj:
            node.Add(_pyobj_to_TNode(x))
        return node
    elif obj is None:
        return TNode()
    else:
        raise Exception('Can\'t convert {} object to TNode'.format(type(obj)))
