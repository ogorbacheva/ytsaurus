package schema

import (
	"sort"
	"testing"

	"github.com/stretchr/testify/require"
)

type innerStruct struct {
	Field int
}

type testBasicTypes struct {
	I   int
	I64 int64
	I32 int32
	I16 int16

	U   uint
	U64 uint64
	U32 uint32
	U16 uint16

	S string
	B []byte

	A0 interface{}
	A1 innerStruct
	A2 map[string]interface{}
}

func TestInfer(t *testing.T) {
	s, err := Infer(&testBasicTypes{})
	require.NoError(t, err)

	require.Equal(t, s, Schema{
		Columns: []Column{
			{Name: "I", Type: TypeInt64, Required: true},
			{Name: "I64", Type: TypeInt64, Required: true},
			{Name: "I32", Type: TypeInt32, Required: true},
			{Name: "I16", Type: TypeInt16, Required: true},
			{Name: "U", Type: TypeUint64, Required: true},
			{Name: "U64", Type: TypeUint64, Required: true},
			{Name: "U32", Type: TypeUint32, Required: true},
			{Name: "U16", Type: TypeUint16, Required: true},
			{Name: "S", Type: TypeString, Required: true},
			{Name: "B", Type: TypeBytes, Required: true},
			{Name: "A0", Type: TypeAny},
			{Name: "A1", Type: TypeAny},
			{Name: "A2", Type: TypeAny},
		},
	})
}

func TestInferType(t *testing.T) {
	var a int
	_, err := Infer(a)
	require.Error(t, err)

	var b string
	_, err = Infer(b)
	require.Error(t, err)

	var c interface{}
	_, err = Infer(c)
	require.Error(t, err)

	d := make(map[interface{}]interface{})
	_, err = Infer(d)
	require.Error(t, err)
}

type textMarhaler struct{}

func (*textMarhaler) MarshalText() (text []byte, err error) {
	panic("implement me")
}

type valueTextMarhaler struct{}

func (valueTextMarhaler) MarshalText() (text []byte, err error) {
	panic("implement me")
}

type binaryMarhaler struct{}

func (*binaryMarhaler) MarshalBinary() (text []byte, err error) {
	panic("implement me")
}

type valueBinaryMarhaler struct{}

func (valueBinaryMarhaler) MarshalBinary() (text []byte, err error) {
	panic("implement me")
}

type testMarshalers struct {
	M0 textMarhaler
	M1 valueTextMarhaler
	M2 binaryMarhaler
	M3 valueBinaryMarhaler

	O0 *textMarhaler
	O1 *valueTextMarhaler
	O2 *binaryMarhaler
	O3 *valueBinaryMarhaler
}

func TestInferMarshalerTypes(t *testing.T) {
	v := &testMarshalers{}

	s, err := Infer(v)
	require.NoError(t, err)

	require.Equal(t, s, Schema{
		Columns: []Column{
			{Name: "M0", Type: TypeString, Required: true},
			{Name: "M1", Type: TypeString, Required: true},
			{Name: "M2", Type: TypeBytes, Required: true},
			{Name: "M3", Type: TypeBytes, Required: true},

			{Name: "O0", Type: TypeString},
			{Name: "O1", Type: TypeString},
			{Name: "O2", Type: TypeBytes},
			{Name: "O3", Type: TypeBytes},
		},
	})
}

type tagStruct struct {
	A int `yson:"a"`
	B int `yson:"-"`
}

func TestInferTags(t *testing.T) {
	s, err := Infer(&tagStruct{})
	require.NoError(t, err)

	require.Equal(t, s, Schema{
		Columns: []Column{
			{Name: "a", Type: TypeInt64, Required: true},
		},
	})
}

type keyStruct struct {
	A int `yson:"a,key"`
	B int `yson:"b"`
}

func TestInferKeyColumns(t *testing.T) {
	s, err := Infer(&keyStruct{})
	require.NoError(t, err)

	require.Equal(t, s, Schema{
		Columns: []Column{
			{Name: "a", Type: TypeInt64, Required: true, SortOrder: SortAscending},
			{Name: "b", Type: TypeInt64, Required: true},
		},
	})
}

type keyDefaultStruct struct {
	A int `yson:",key"`
}

func TestInferKeyWithDefaultName(t *testing.T) {
	s, err := Infer(&keyDefaultStruct{})
	require.NoError(t, err)

	require.Equal(t, s, Schema{
		Columns: []Column{
			{Name: "A", Type: TypeInt64, Required: true, SortOrder: SortAscending},
		},
	})
}

func TestInferMapType(t *testing.T) {
	var a int
	_, err := Infer(a)
	require.Error(t, err)

	var b string
	_, err = Infer(b)
	require.Error(t, err)

	var c interface{}
	_, err = Infer(c)
	require.Error(t, err)

	_, err = InferMap(&testBasicTypes{})
	require.Error(t, err)
}

func TestInferMapNonStringKeys(t *testing.T) {
	a := make(map[int]interface{})
	_, err := Infer(a)
	require.Error(t, err)
}

func TestInferMap(t *testing.T) {
	var I int
	var I64 int64
	var I32 int32
	var I16 int16

	var U uint
	var U64 uint64
	var U32 uint32
	var U16 uint16

	var S string
	var B []byte

	var A0 interface{}
	var A1 innerStruct
	var A2 map[string]interface{}

	testMap := make(map[string]interface{})
	testMap["I"] = I
	testMap["I64"] = I64
	testMap["I32"] = I32
	testMap["I16"] = I16

	testMap["U"] = U
	testMap["U64"] = U64
	testMap["U32"] = U32
	testMap["U16"] = U16

	testMap["S"] = S
	testMap["B"] = B

	testMap["A0"] = A0
	testMap["A1"] = A1
	testMap["A2"] = A2

	s, err := InferMap(testMap)
	require.NoError(t, err)

	columnsExpected := []Column{
		{Name: "I", Type: TypeInt64, Required: true},
		{Name: "I64", Type: TypeInt64, Required: true},
		{Name: "I32", Type: TypeInt32, Required: true},
		{Name: "I16", Type: TypeInt16, Required: true},
		{Name: "U", Type: TypeUint64, Required: true},
		{Name: "U64", Type: TypeUint64, Required: true},
		{Name: "U32", Type: TypeUint32, Required: true},
		{Name: "U16", Type: TypeUint16, Required: true},
		{Name: "S", Type: TypeString, Required: true},
		{Name: "B", Type: TypeBytes, Required: true},
		{Name: "A0", Type: TypeAny},
		{Name: "A1", Type: TypeAny},
		{Name: "A2", Type: TypeAny},
	}
	sort.Slice(columnsExpected, func(i, j int) bool {
		return columnsExpected[i].Name < columnsExpected[j].Name
	})

	require.Equal(t, s, Schema{Columns: columnsExpected})
}
