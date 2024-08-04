# C++ skeleton for Bison

# Copyright (C) 2002-2015, 2018-2020 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


## --------- ##
## variant.  ##
## --------- ##

# b4_symbol_variant(YYTYPE, YYVAL, ACTION, [ARGS])
# ------------------------------------------------
# Run some ACTION ("build", or "destroy") on YYVAL of symbol type
# YYTYPE.
m4_define([b4_symbol_variant],
[m4_pushdef([b4_dollar_dollar],
            [$2.$3< $][3 > (m4_shift3($@))])dnl
switch ($1)
    {
b4_type_foreach([_b4_type_action])[]dnl
      default:
        break;
    }
m4_popdef([b4_dollar_dollar])dnl
])


# _b4_char_sizeof_counter
# -----------------------
# A counter used by _b4_char_sizeof_dummy to create fresh symbols.
m4_define([_b4_char_sizeof_counter],
[0])

# _b4_char_sizeof_dummy
# ---------------------
# At each call return a new C++ identifier.
m4_define([_b4_char_sizeof_dummy],
[m4_define([_b4_char_sizeof_counter], m4_incr(_b4_char_sizeof_counter))dnl
dummy[]_b4_char_sizeof_counter])


# b4_char_sizeof(SYMBOL-NUMS)
# ---------------------------
# To be mapped on the list of type names to produce:
#
#    char dummy1[sizeof (type_name_1)];
#    char dummy2[sizeof (type_name_2)];
#
# for defined type names.
m4_define([b4_char_sizeof],
[b4_symbol_if([$1], [has_type],
[
m4_map([      b4_symbol_tag_comment], [$@])dnl
      char _b4_char_sizeof_dummy@{sizeof (b4_symbol([$1], [type]))@};
])])


# b4_variant_includes
# -------------------
# The needed includes for variants support.
m4_define([b4_variant_includes],
[b4_parse_assert_if([[#include <typeinfo>]])[
#ifndef YY_ASSERT
# include <cassert>
# define YY_ASSERT assert
#endif
]])



## -------------------------- ##
## Adjustments for variants.  ##
## -------------------------- ##


# b4_value_type_declare
# ---------------------
# Define semantic_type.
m4_define([b4_value_type_declare],
[[  /// A buffer to store and retrieve objects.
  ///
  /// Sort of a variant, but does not keep track of the nature
  /// of the stored data, since that knowledge is available
  /// via the current parser state.
  class semantic_type
  {
  public:
    /// Type of *this.
    typedef semantic_type self_type;

    /// Empty construction.
    semantic_type () YY_NOEXCEPT
      : yybuffer_ ()]b4_parse_assert_if([
      , yytypeid_ (YY_NULLPTR)])[
    {}

    /// Construct and fill.
    template <typename T>
    semantic_type (YY_RVREF (T) t)]b4_parse_assert_if([
      : yytypeid_ (&typeid (T))])[
    {
      YY_ASSERT (sizeof (T) <= size);
      new (yyas_<T> ()) T (YY_MOVE (t));
    }

    /// Destruction, allowed only if empty.
    ~semantic_type () YY_NOEXCEPT
    {]b4_parse_assert_if([
      YY_ASSERT (!yytypeid_);
    ])[}

# if 201103L <= YY_CPLUSPLUS
    /// Instantiate a \a T in here from \a t.
    template <typename T, typename... U>
    T&
    emplace (U&&... u)
    {]b4_parse_assert_if([
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);])[
      return *new (yyas_<T> ()) T (std::forward <U>(u)...);
    }
# else
    /// Instantiate an empty \a T in here.
    template <typename T>
    T&
    emplace ()
    {]b4_parse_assert_if([
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);])[
      return *new (yyas_<T> ()) T ();
    }

    /// Instantiate a \a T in here from \a t.
    template <typename T>
    T&
    emplace (const T& t)
    {]b4_parse_assert_if([
      YY_ASSERT (!yytypeid_);
      YY_ASSERT (sizeof (T) <= size);
      yytypeid_ = & typeid (T);])[
      return *new (yyas_<T> ()) T (t);
    }
# endif

    /// Instantiate an empty \a T in here.
    /// Obsolete, use emplace.
    template <typename T>
    T&
    build ()
    {
      return emplace<T> ();
    }

    /// Instantiate a \a T in here from \a t.
    /// Obsolete, use emplace.
    template <typename T>
    T&
    build (const T& t)
    {
      return emplace<T> (t);
    }

    /// Accessor to a built \a T.
    template <typename T>
    T&
    as () YY_NOEXCEPT
    {]b4_parse_assert_if([
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == typeid (T));
      YY_ASSERT (sizeof (T) <= size);])[
      return *yyas_<T> ();
    }

    /// Const accessor to a built \a T (for %printer).
    template <typename T>
    const T&
    as () const YY_NOEXCEPT
    {]b4_parse_assert_if([
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == typeid (T));
      YY_ASSERT (sizeof (T) <= size);])[
      return *yyas_<T> ();
    }

    /// Swap the content with \a that, of same type.
    ///
    /// Both variants must be built beforehand, because swapping the actual
    /// data requires reading it (with as()), and this is not possible on
    /// unconstructed variants: it would require some dynamic testing, which
    /// should not be the variant's responsibility.
    /// Swapping between built and (possibly) non-built is done with
    /// self_type::move ().
    template <typename T>
    void
    swap (self_type& that) YY_NOEXCEPT
    {]b4_parse_assert_if([
      YY_ASSERT (yytypeid_);
      YY_ASSERT (*yytypeid_ == *that.yytypeid_);])[
      std::swap (as<T> (), that.as<T> ());
    }

    /// Move the content of \a that to this.
    ///
    /// Destroys \a that.
    template <typename T>
    void
    move (self_type& that)
    {
# if 201103L <= YY_CPLUSPLUS
      emplace<T> (std::move (that.as<T> ()));
# else
      emplace<T> ();
      swap<T> (that);
# endif
      that.destroy<T> ();
    }

# if 201103L <= YY_CPLUSPLUS
    /// Move the content of \a that to this.
    template <typename T>
    void
    move (self_type&& that)
    {
      emplace<T> (std::move (that.as<T> ()));
      that.destroy<T> ();
    }
#endif

    /// Copy the content of \a that to this.
    template <typename T>
    void
    copy (const self_type& that)
    {
      emplace<T> (that.as<T> ());
    }

    /// Destroy the stored \a T.
    template <typename T>
    void
    destroy ()
    {
      as<T> ().~T ();]b4_parse_assert_if([
      yytypeid_ = YY_NULLPTR;])[
    }

  private:
    /// Prohibit blind copies.
    self_type& operator= (const self_type&);
    semantic_type (const self_type&);

    /// Accessor to raw memory as \a T.
    template <typename T>
    T*
    yyas_ () YY_NOEXCEPT
    {
      void *yyp = yybuffer_.yyraw;
      return static_cast<T*> (yyp);
     }

    /// Const accessor to raw memory as \a T.
    template <typename T>
    const T*
    yyas_ () const YY_NOEXCEPT
    {
      const void *yyp = yybuffer_.yyraw;
      return static_cast<const T*> (yyp);
     }

    /// An auxiliary type to compute the largest semantic type.
    union union_type
    {]b4_type_foreach([b4_char_sizeof])[    };

    /// The size of the largest semantic type.
    enum { size = sizeof (union_type) };

    /// A buffer to store semantic values.
    union
    {
      /// Strongest alignment constraints.
      long double yyalign_me;
      /// A buffer large enough to store any of the semantic values.
      char yyraw[size];
    } yybuffer_;]b4_parse_assert_if([

    /// Whether the content is built: if defined, the name of the stored type.
    const std::type_info *yytypeid_;])[
  };
]])


# How the semantic value is extracted when using variants.

# b4_symbol_value(VAL, SYMBOL-NUM, [TYPE])
# ----------------------------------------
# See README.
m4_define([b4_symbol_value],
[m4_ifval([$3],
          [$1.as< $3 > ()],
          [m4_ifval([$2],
                    [b4_symbol_if([$2], [has_type],
                                  [$1.as < b4_symbol([$2], [type]) > ()],
                                  [$1])],
                    [$1])])])

# b4_symbol_value_template(VAL, SYMBOL-NUM, [TYPE])
# -------------------------------------------------
# Same as b4_symbol_value, but used in a template method.
m4_define([b4_symbol_value_template],
[m4_ifval([$3],
          [$1.template as< $3 > ()],
          [m4_ifval([$2],
                    [b4_symbol_if([$2], [has_type],
                                  [$1.template as < b4_symbol([$2], [type]) > ()],
                                  [$1])],
                    [$1])])])



## ------------- ##
## make_SYMBOL.  ##
## ------------- ##


# _b4_includes_tokens(SYMBOL-NUM...)
# ----------------------------------
# Expands to non-empty iff one of the SYMBOL-NUM denotes
# a token.
m4_define([_b4_is_token],
          [b4_symbol_if([$1], [is_token], [1])])
m4_define([_b4_includes_tokens],
          [m4_map([_b4_is_token], [$@])])


# _b4_token_maker_define(SYMBOL-NUM)
# ----------------------------------
# Declare make_SYMBOL for SYMBOL-NUM.  Use at class-level.
m4_define([_b4_token_maker_define],
[b4_token_visible_if([$1],
[#if 201103L <= YY_CPLUSPLUS
      static
      symbol_type
      make_[]_b4_symbol([$1], [id]) (b4_join(
                 b4_symbol_if([$1], [has_type],
                 [b4_symbol([$1], [type]) v]),
                 b4_locations_if([location_type l])))
      {
        return symbol_type (b4_join([token::b4_symbol([$1], [id])],
                                    b4_symbol_if([$1], [has_type], [std::move (v)]),
                                    b4_locations_if([std::move (l)])));
      }
#else
      static
      symbol_type
      make_[]_b4_symbol([$1], [id]) (b4_join(
                 b4_symbol_if([$1], [has_type],
                 [const b4_symbol([$1], [type])& v]),
                 b4_locations_if([const location_type& l])))
      {
        return symbol_type (b4_join([token::b4_symbol([$1], [id])],
                                    b4_symbol_if([$1], [has_type], [v]),
                                    b4_locations_if([l])));
      }
#endif
])])


m4_define([_b4_type_clause],
[b4_symbol_if([$1], [is_token],
              [b4_symbol_if([$1], [has_id],
                            [tok == token::b4_symbol([$1], [id])],
                            [tok == b4_symbol([$1], [user_number])])])])


# _b4_token_constructor_define(SYMBOL-NUM...)
# -------------------------------------------
# Define a unique make_symbol for all the SYMBOL-NUM (they
# have the same type).  Use at class-level.
m4_define([_b4_token_constructor_define],
[m4_ifval(_b4_includes_tokens($@),
[[#if 201103L <= YY_CPLUSPLUS
      symbol_type (]b4_join(
          [int tok],
          b4_symbol_if([$1], [has_type],
                       [b4_symbol([$1], [type]) v]),
          b4_locations_if([location_type l]))[)
        : super_type(]b4_join([token_type (tok)],
                              b4_symbol_if([$1], [has_type], [std::move (v)]),
                              b4_locations_if([std::move (l)]))[)
      {
        YY_ASSERT (]m4_join([ || ], m4_map_sep([_b4_type_clause], [, ], [$@]))[);
      }
#else
      symbol_type (]b4_join(
          [int tok],
          b4_symbol_if([$1], [has_type],
                       [const b4_symbol([$1], [type])& v]),
          b4_locations_if([const location_type& l]))[)
        : super_type(]b4_join([token_type (tok)],
                              b4_symbol_if([$1], [has_type], [v]),
                              b4_locations_if([l]))[)
      {
        YY_ASSERT (]m4_join([ || ], m4_map_sep([_b4_type_clause], [, ], [$@]))[);
      }
#endif
]])])


# b4_basic_symbol_constructor_define(SYMBOL-NUM)
# ----------------------------------------------
# Generate a constructor for basic_symbol from given type.
m4_define([b4_basic_symbol_constructor_define],
[[#if 201103L <= YY_CPLUSPLUS
      basic_symbol (]b4_join(
          [typename Base::kind_type t],
          b4_symbol_if([$1], [has_type], [b4_symbol([$1], [type])&& v]),
          b4_locations_if([location_type&& l]))[)
        : Base (t)]b4_symbol_if([$1], [has_type], [
        , value (std::move (v))])[]b4_locations_if([
        , location (std::move (l))])[
      {}
#else
      basic_symbol (]b4_join(
          [typename Base::kind_type t],
          b4_symbol_if([$1], [has_type], [const b4_symbol([$1], [type])& v]),
          b4_locations_if([const location_type& l]))[)
        : Base (t)]b4_symbol_if([$1], [has_type], [
        , value (v)])[]b4_locations_if([
        , location (l)])[
      {}
#endif
]])


# b4_token_constructor_define
# ---------------------------
# Define the overloaded versions of make_symbol for all the value types.
m4_define([b4_token_constructor_define],
[    // Implementation of make_symbol for each symbol type.
b4_symbol_foreach([_b4_token_maker_define])])
