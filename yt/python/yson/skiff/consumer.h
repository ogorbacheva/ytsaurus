#pragma once

#include "record.h"
#include "public.h"
#include "schema.h"

#include <yt/python/common/helpers.h>
#include <yt/python/common/stream.h>

#include <Extensions.hxx> // pycxx
#include <Objects.hxx> // pycxx

#include <Python.h>

namespace NYT {
namespace NPython {

////////////////////////////////////////////////////////////////////////////////

class TPythonSkiffRecordBuilder
{
public:
    explicit TPythonSkiffRecordBuilder(const std::vector<Py::PythonClassObject<TSkiffSchemaPython>>& schemas);

    void OnBeginRow(ui16 schemaIndex);
    void OnEndRow();
    void OnStringScalar(const TStringBuf& value, ui16 columnId);
    void OnInt64Scalar(i64 value, ui16 columnId);
    void OnUint64Scalar(ui64 value, ui16 columnId);
    void OnDoubleScalar(double value, ui16 columnId);
    void OnBooleanScalar(bool value, ui16 columnId);
    void OnEntity(ui16 columnId);
    void OnYsonString(const TStringBuf& value, ui16 columnId);

    void OnOtherColumns(const TStringBuf& value);

    Py::Object ExtractObject();
    bool HasObject() const;

private:
    std::vector<Py::PythonClassObject<TSkiffSchemaPython>> Schemas_;
    std::queue<Py::Object> Objects_;

    TIntrusivePtr<TSkiffRecord> CurrentRecord_;
    Py::Object CurrentSchema_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NPython
} // namespace NYT
