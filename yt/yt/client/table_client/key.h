#pragma once

#include "unversioned_row.h"

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

//! This class represents a (contextually) schemaful comparable row. It behaves
//! similarly to TUnversioned{,Owning}Row and is implemented as a strong alias
//! to the corresponding type via inheritance.
template <class TRow>
class TKeyImpl
    : public TRow
{
public:
    //! Construct from a given row and validate that row does not contain
    //! setntinels of types Min, Max and Bottom.
    static TKeyImpl FromRow(const TRow& row);

    //! Same as previous but for rvalue refs.
    static TKeyImpl FromRow(TRow&& row);

    //! Construct from a given row without checking presence of types Min, Max and Bottom.
    //! NB: in debug mode value type check is still performed, but results in YT_ABORT().
    static TKeyImpl FromRowUnchecked(const TRow& row);

    //! Same as previous but for rvalue refs.
    static TKeyImpl FromRowUnchecked(TRow&& row);

    //! Helper for static_cast<const TRow&>(*this).
    const TRow& AsRow() const;

private:
    TKeyImpl() = default;

    static void ValidateValueTypes(const TRow& row);
};

////////////////////////////////////////////////////////////////////////////////

}

// Template is explicitly instantiated for the following two kinds of rows.
using TKey = NDetail::TKeyImpl<TUnversionedRow>;
using TOwningKey = NDetail::TKeyImpl<TUnversionedOwningRow>;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
