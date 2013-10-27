// A Bison parser, made by GNU Bison 3.0.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2013 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// Take the name prefix into account.
#define yylex   yt_ql_yylex

// First part of user declarations.

#line 39 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:398

# ifndef YY_NULL
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULL nullptr
#  else
#   define YY_NULL 0
#  endif
# endif

#include "parser.hpp"

// User implementation prologue.

#line 53 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:406
// Unqualified %code blocks.
#line 31 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:407

    #include <ytlib/query_client/lexer.h>
    #define yt_ql_yylex lexer.GetNextToken

#line 60 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:407


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (/*CONSTCOND*/ false)
# endif


// Suppress unused-variable warnings by "using" E.
#define YYUSE(E) ((void) (E))

// Enable debugging if requested.
#if YT_QL_YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << std::endl;                  \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yystack_print_ ();                \
  } while (false)

#else // !YT_QL_YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YYUSE(Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void>(0)
# define YY_STACK_PRINT()                static_cast<void>(0)

#endif // !YT_QL_YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyempty = true)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

#line 5 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:473
namespace NYT { namespace NQueryClient {
#line 146 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:473

  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  std::string
  TParser::yytnamerr_ (const char *yystr)
  {
    if (*yystr == '"')
      {
        std::string yyr = "";
        char const *yyp = yystr;

        for (;;)
          switch (*++yyp)
            {
            case '\'':
            case ',':
              goto do_not_strip_quotes;

            case '\\':
              if (*++yyp != '\\')
                goto do_not_strip_quotes;
              // Fall through.
            default:
              yyr += *yyp;
              break;

            case '"':
              return yyr;
            }
      do_not_strip_quotes: ;
      }

    return yystr;
  }


  /// Build a parser object.
  TParser::TParser (TLexer& lexer_yyarg, TQueryContext* context_yyarg, const TOperator** head_yyarg)
    :
#if YT_QL_YYDEBUG
      yydebug_ (false),
      yycdebug_ (&std::cerr),
#endif
      lexer (lexer_yyarg),
      context (context_yyarg),
      head (head_yyarg)
  {}

  TParser::~TParser ()
  {}


  /*---------------.
  | Symbol types.  |
  `---------------*/

  inline
  TParser::syntax_error::syntax_error (const location_type& l, const std::string& m)
    : std::runtime_error (m)
    , location (l)
  {}

  // basic_symbol.
  template <typename Base>
  inline
  TParser::basic_symbol<Base>::basic_symbol ()
    : value ()
  {}

  template <typename Base>
  inline
  TParser::basic_symbol<Base>::basic_symbol (const basic_symbol& other)
    : Base (other)
    , value ()
    , location (other.location)
  {
      switch (other.type_get ())
    {
      case 33: // binary-rel-op
        value.copy< EBinaryOp > (other.value);
        break;

      case 32: // binary-rel-op-expr
        value.copy< TBinaryOpExpression* > (other.value);
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.copy< TExpression* > (other.value);
        break;

      case 29: // function-expr
        value.copy< TFunctionExpression* > (other.value);
        break;

      case 30: // function-expr-args
        value.copy< TFunctionExpression::TArguments > (other.value);
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.copy< TOperator* > (other.value);
        break;

      case 26: // projections
        value.copy< TProjectOperator::TProjections > (other.value);
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.copy< TStringBuf > (other.value);
        break;

      case 8: // "double literal"
        value.copy< double > (other.value);
        break;

      case 7: // "integer literal"
        value.copy< i64 > (other.value);
        break;

      default:
        break;
    }

  }


  template <typename Base>
  inline
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const semantic_type& v, const location_type& l)
    : Base (t)
    , value ()
    , location (l)
  {
    (void) v;
      switch (this->type_get ())
    {
      case 33: // binary-rel-op
        value.copy< EBinaryOp > (v);
        break;

      case 32: // binary-rel-op-expr
        value.copy< TBinaryOpExpression* > (v);
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.copy< TExpression* > (v);
        break;

      case 29: // function-expr
        value.copy< TFunctionExpression* > (v);
        break;

      case 30: // function-expr-args
        value.copy< TFunctionExpression::TArguments > (v);
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.copy< TOperator* > (v);
        break;

      case 26: // projections
        value.copy< TProjectOperator::TProjections > (v);
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.copy< TStringBuf > (v);
        break;

      case 8: // "double literal"
        value.copy< double > (v);
        break;

      case 7: // "integer literal"
        value.copy< i64 > (v);
        break;

      default:
        break;
    }
}


  // Implementation of basic_symbol constructor for each type.

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const location_type& l)
    : Base (t)
    , value ()
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const EBinaryOp v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TBinaryOpExpression* v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TExpression* v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TFunctionExpression* v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TFunctionExpression::TArguments v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TOperator* v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TProjectOperator::TProjections v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const TStringBuf v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const double v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}

  template <typename Base>
  TParser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const i64 v, const location_type& l)
    : Base (t)
    , value (v)
    , location (l)
  {}


  template <typename Base>
  inline
  TParser::basic_symbol<Base>::~basic_symbol ()
  {
    // User destructor.
    symbol_number_type yytype = this->type_get ();
    switch (yytype)
    {
   default:
      break;
    }

    // Type destructor.
    switch (yytype)
    {
      case 33: // binary-rel-op
        value.template destroy< EBinaryOp > ();
        break;

      case 32: // binary-rel-op-expr
        value.template destroy< TBinaryOpExpression* > ();
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.template destroy< TExpression* > ();
        break;

      case 29: // function-expr
        value.template destroy< TFunctionExpression* > ();
        break;

      case 30: // function-expr-args
        value.template destroy< TFunctionExpression::TArguments > ();
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.template destroy< TOperator* > ();
        break;

      case 26: // projections
        value.template destroy< TProjectOperator::TProjections > ();
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.template destroy< TStringBuf > ();
        break;

      case 8: // "double literal"
        value.template destroy< double > ();
        break;

      case 7: // "integer literal"
        value.template destroy< i64 > ();
        break;

      default:
        break;
    }

  }

  template <typename Base>
  inline
  void
  TParser::basic_symbol<Base>::move (basic_symbol& s)
  {
    super_type::move(s);
      switch (this->type_get ())
    {
      case 33: // binary-rel-op
        value.move< EBinaryOp > (s.value);
        break;

      case 32: // binary-rel-op-expr
        value.move< TBinaryOpExpression* > (s.value);
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.move< TExpression* > (s.value);
        break;

      case 29: // function-expr
        value.move< TFunctionExpression* > (s.value);
        break;

      case 30: // function-expr-args
        value.move< TFunctionExpression::TArguments > (s.value);
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.move< TOperator* > (s.value);
        break;

      case 26: // projections
        value.move< TProjectOperator::TProjections > (s.value);
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.move< TStringBuf > (s.value);
        break;

      case 8: // "double literal"
        value.move< double > (s.value);
        break;

      case 7: // "integer literal"
        value.move< i64 > (s.value);
        break;

      default:
        break;
    }

    location = s.location;
  }

  // by_type.
  inline
  TParser::by_type::by_type ()
     : type (empty)
  {}

  inline
  TParser::by_type::by_type (const by_type& other)
    : type (other.type)
  {}

  inline
  TParser::by_type::by_type (token_type t)
    : type (yytranslate_ (t))
  {}

  inline
  void
  TParser::by_type::move (by_type& that)
  {
    type = that.type;
    that.type = empty;
  }

  inline
  int
  TParser::by_type::type_get () const
  {
    return type;
  }
  // Implementation of make_symbol for each symbol type.
  TParser::symbol_type
  TParser::make_End (const location_type& l)
  {
    return symbol_type (token::End, l);

  }

  TParser::symbol_type
  TParser::make_Failure (const location_type& l)
  {
    return symbol_type (token::Failure, l);

  }

  TParser::symbol_type
  TParser::make_KwFrom (const location_type& l)
  {
    return symbol_type (token::KwFrom, l);

  }

  TParser::symbol_type
  TParser::make_KwWhere (const location_type& l)
  {
    return symbol_type (token::KwWhere, l);

  }

  TParser::symbol_type
  TParser::make_Identifier (const TStringBuf& v, const location_type& l)
  {
    return symbol_type (token::Identifier, v, l);

  }

  TParser::symbol_type
  TParser::make_IntegerLiteral (const i64& v, const location_type& l)
  {
    return symbol_type (token::IntegerLiteral, v, l);

  }

  TParser::symbol_type
  TParser::make_DoubleLiteral (const double& v, const location_type& l)
  {
    return symbol_type (token::DoubleLiteral, v, l);

  }

  TParser::symbol_type
  TParser::make_YPathLiteral (const TStringBuf& v, const location_type& l)
  {
    return symbol_type (token::YPathLiteral, v, l);

  }

  TParser::symbol_type
  TParser::make_LeftParenthesis (const location_type& l)
  {
    return symbol_type (token::LeftParenthesis, l);

  }

  TParser::symbol_type
  TParser::make_RightParenthesis (const location_type& l)
  {
    return symbol_type (token::RightParenthesis, l);

  }

  TParser::symbol_type
  TParser::make_Asterisk (const location_type& l)
  {
    return symbol_type (token::Asterisk, l);

  }

  TParser::symbol_type
  TParser::make_Comma (const location_type& l)
  {
    return symbol_type (token::Comma, l);

  }

  TParser::symbol_type
  TParser::make_OpLess (const location_type& l)
  {
    return symbol_type (token::OpLess, l);

  }

  TParser::symbol_type
  TParser::make_OpLessOrEqual (const location_type& l)
  {
    return symbol_type (token::OpLessOrEqual, l);

  }

  TParser::symbol_type
  TParser::make_OpEqual (const location_type& l)
  {
    return symbol_type (token::OpEqual, l);

  }

  TParser::symbol_type
  TParser::make_OpNotEqual (const location_type& l)
  {
    return symbol_type (token::OpNotEqual, l);

  }

  TParser::symbol_type
  TParser::make_OpGreater (const location_type& l)
  {
    return symbol_type (token::OpGreater, l);

  }

  TParser::symbol_type
  TParser::make_OpGreaterOrEqual (const location_type& l)
  {
    return symbol_type (token::OpGreaterOrEqual, l);

  }



  // by_state.
  inline
  TParser::by_state::by_state ()
    : state (empty)
  {}

  inline
  TParser::by_state::by_state (const by_state& other)
    : state (other.state)
  {}

  inline
  void
  TParser::by_state::move (by_state& that)
  {
    state = that.state;
    that.state = empty;
  }

  inline
  TParser::by_state::by_state (state_type s)
    : state (s)
  {}

  inline
  TParser::symbol_number_type
  TParser::by_state::type_get () const
  {
    return state == empty ? 0 : yystos_[state];
  }

  inline
  TParser::stack_symbol_type::stack_symbol_type ()
  {}


  inline
  TParser::stack_symbol_type::stack_symbol_type (state_type s, symbol_type& that)
    : super_type (s, that.location)
  {
      switch (that.type_get ())
    {
      case 33: // binary-rel-op
        value.move< EBinaryOp > (that.value);
        break;

      case 32: // binary-rel-op-expr
        value.move< TBinaryOpExpression* > (that.value);
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.move< TExpression* > (that.value);
        break;

      case 29: // function-expr
        value.move< TFunctionExpression* > (that.value);
        break;

      case 30: // function-expr-args
        value.move< TFunctionExpression::TArguments > (that.value);
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.move< TOperator* > (that.value);
        break;

      case 26: // projections
        value.move< TProjectOperator::TProjections > (that.value);
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.move< TStringBuf > (that.value);
        break;

      case 8: // "double literal"
        value.move< double > (that.value);
        break;

      case 7: // "integer literal"
        value.move< i64 > (that.value);
        break;

      default:
        break;
    }

    // that is emptied.
    that.type = empty;
  }

  inline
  TParser::stack_symbol_type&
  TParser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
      switch (that.type_get ())
    {
      case 33: // binary-rel-op
        value.copy< EBinaryOp > (that.value);
        break;

      case 32: // binary-rel-op-expr
        value.copy< TBinaryOpExpression* > (that.value);
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        value.copy< TExpression* > (that.value);
        break;

      case 29: // function-expr
        value.copy< TFunctionExpression* > (that.value);
        break;

      case 30: // function-expr-args
        value.copy< TFunctionExpression::TArguments > (that.value);
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        value.copy< TOperator* > (that.value);
        break;

      case 26: // projections
        value.copy< TProjectOperator::TProjections > (that.value);
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        value.copy< TStringBuf > (that.value);
        break;

      case 8: // "double literal"
        value.copy< double > (that.value);
        break;

      case 7: // "integer literal"
        value.copy< i64 > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    return *this;
  }


  template <typename Base>
  inline
  void
  TParser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YT_QL_YYDEBUG
  template <typename Base>
  void
  TParser::yy_print_ (std::ostream& yyo,
                                     const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YYUSE (yyoutput);
    symbol_number_type yytype = yysym.type_get ();
    yyo << (yytype < yyntokens_ ? "token" : "nterm")
        << ' ' << yytname_[yytype] << " ("
        << yysym.location << ": ";
    YYUSE (yytype);
    yyo << ')';
  }
#endif

  inline
  void
  TParser::yypush_ (const char* m, state_type s, symbol_type& sym)
  {
    stack_symbol_type t (s, sym);
    yypush_ (m, t);
  }

  inline
  void
  TParser::yypush_ (const char* m, stack_symbol_type& s)
  {
    if (m)
      YY_SYMBOL_PRINT (m, s);
    yystack_.push (s);
  }

  inline
  void
  TParser::yypop_ (unsigned int n)
  {
    yystack_.pop (n);
  }

#if YT_QL_YYDEBUG
  std::ostream&
  TParser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  TParser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  TParser::debug_level_type
  TParser::debug_level () const
  {
    return yydebug_;
  }

  void
  TParser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YT_QL_YYDEBUG

  inline TParser::state_type
  TParser::yy_lr_goto_state_ (state_type yystate, int yylhs)
  {
    int yyr = yypgoto_[yylhs - yyntokens_] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yylhs - yyntokens_];
  }

  inline bool
  TParser::yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  inline bool
  TParser::yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  int
  TParser::parse ()
  {
    /// Whether yyla contains a lookahead.
    bool yyempty = true;

    // State.
    int yyn;
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// $$ and @$.
    stack_symbol_type yylhs;

    /// The return value of parse ().
    int yyresult;

    // FIXME: This shoud be completely indented.  It is not yet to
    // avoid gratuitous conflicts when merging into the master branch.
    try
      {
    YYCDEBUG << "Starting parse" << std::endl;


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULL, 0, yyla);

    // A new symbol was pushed on the stack.
  yynewstate:
    YYCDEBUG << "Entering state " << yystack_[0].state << std::endl;

    // Accept?
    if (yystack_[0].state == yyfinal_)
      goto yyacceptlab;

    goto yybackup;

    // Backup.
  yybackup:

    // Try to take a decision without lookahead.
    yyn = yypact_[yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyempty)
      {
        YYCDEBUG << "Reading a token: ";
        try
          {
            yyla.type = yytranslate_ (yylex (&yyla.value, &yyla.location));
          }
        catch (const syntax_error& yyexc)
          {
            error (yyexc);
            goto yyerrlab1;
          }
        yyempty = false;
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.type_get ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.type_get ())
      goto yydefault;

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Discard the token being shifted.
    yyempty = true;

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", yyn, yyla);
    goto yynewstate;

  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;

  /*-----------------------------.
  | yyreduce -- Do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    yylhs.state = yy_lr_goto_state_(yystack_[yylen].state, yyr1_[yyn]);
    /* Variants are always initialized to an empty instance of the
       correct type. The default $$=$1 action is NOT applied when using
       variants.  */
      switch (yyr1_[yyn])
    {
      case 33: // binary-rel-op
        yylhs.value.build< EBinaryOp > ();
        break;

      case 32: // binary-rel-op-expr
        yylhs.value.build< TBinaryOpExpression* > ();
        break;

      case 27: // projection
      case 28: // atomic-expr
      case 31: // function-expr-arg
        yylhs.value.build< TExpression* > ();
        break;

      case 29: // function-expr
        yylhs.value.build< TFunctionExpression* > ();
        break;

      case 30: // function-expr-args
        yylhs.value.build< TFunctionExpression::TArguments > ();
        break;

      case 22: // select-clause
      case 23: // select-source
      case 24: // from-where-clause
      case 25: // from-clause
        yylhs.value.build< TOperator* > ();
        break;

      case 26: // projections
        yylhs.value.build< TProjectOperator::TProjections > ();
        break;

      case 6: // "identifier"
      case 9: // "YPath literal"
        yylhs.value.build< TStringBuf > ();
        break;

      case 8: // "double literal"
        yylhs.value.build< double > ();
        break;

      case 7: // "integer literal"
        yylhs.value.build< i64 > ();
        break;

      default:
        break;
    }


    // Compute the default @$.
    {
      slice<stack_symbol_type, stack_type> slice (yystack_, yylen);
      YYLLOC_DEFAULT (yylhs.location, slice, yylen);
    }

    // Perform the reduction.
    YY_REDUCE_PRINT (yyn);
    try
      {
        switch (yyn)
          {
  case 2:
#line 86 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            *head = yystack_[0].value.as< TOperator* > ();
        }
#line 1157 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 3:
#line 93 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            auto projectOp = new (context) TProjectOperator(context, yystack_[0].value.as< TOperator* > ());
            projectOp->Projections().assign(yystack_[1].value.as< TProjectOperator::TProjections > ().begin(), yystack_[1].value.as< TProjectOperator::TProjections > ().end());
            yylhs.value.as< TOperator* > () = projectOp;
        }
#line 1167 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 4:
#line 102 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< TOperator* > () = yystack_[0].value.as< TOperator* > (); }
#line 1173 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 5:
#line 107 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TOperator* > () = yystack_[0].value.as< TOperator* > ();
        }
#line 1181 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 6:
#line 111 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            auto filterOp = new (context) TFilterOperator(context, yystack_[2].value.as< TOperator* > ());
            filterOp->SetPredicate(yystack_[0].value.as< TBinaryOpExpression* > ());
            yylhs.value.as< TOperator* > () = filterOp;
        }
#line 1191 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 7:
#line 120 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            auto tableIndex = context->GetTableIndexByAlias("");
            auto scanOp = new (context) TScanOperator(context, tableIndex);
            context->BindToTableIndex(tableIndex, yystack_[0].value.as< TStringBuf > (), scanOp);
            yylhs.value.as< TOperator* > () = scanOp;
        }
#line 1202 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 8:
#line 130 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TProjectOperator::TProjections > ().swap(yystack_[2].value.as< TProjectOperator::TProjections > ());
            yylhs.value.as< TProjectOperator::TProjections > ().push_back(yystack_[0].value.as< TExpression* > ());
        }
#line 1211 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 9:
#line 135 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TProjectOperator::TProjections > ().push_back(yystack_[0].value.as< TExpression* > ());
        }
#line 1219 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 10:
#line 142 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< TExpression* > () = yystack_[0].value.as< TExpression* > (); }
#line 1225 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 11:
#line 144 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< TExpression* > () = yystack_[0].value.as< TFunctionExpression* > (); }
#line 1231 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 12:
#line 149 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            auto tableIndex = context->GetTableIndexByAlias("");
            yylhs.value.as< TExpression* > () = new (context) TReferenceExpression(
                context,
                yylhs.location,
                tableIndex,
                yystack_[0].value.as< TStringBuf > ());
        }
#line 1244 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 13:
#line 158 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TExpression* > () = new (context) TIntegerLiteralExpression(context, yylhs.location, yystack_[0].value.as< i64 > ());
        }
#line 1252 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 14:
#line 162 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TExpression* > () = new (context) TDoubleLiteralExpression(context, yylhs.location, yystack_[0].value.as< double > ());
        }
#line 1260 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 15:
#line 169 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TFunctionExpression* > () = new (context) TFunctionExpression(
                context,
                yylhs.location,
                yystack_[3].value.as< TStringBuf > ());
            yylhs.value.as< TFunctionExpression* > ()->Arguments().assign(yystack_[1].value.as< TFunctionExpression::TArguments > ().begin(), yystack_[1].value.as< TFunctionExpression::TArguments > ().end());
        }
#line 1272 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 16:
#line 180 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TFunctionExpression::TArguments > ().swap(yystack_[2].value.as< TFunctionExpression::TArguments > ());
            yylhs.value.as< TFunctionExpression::TArguments > ().push_back(yystack_[0].value.as< TExpression* > ());
        }
#line 1281 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 17:
#line 185 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TFunctionExpression::TArguments > ().push_back(yystack_[0].value.as< TExpression* > ());
        }
#line 1289 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 18:
#line 192 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< TExpression* > () = yystack_[0].value.as< TExpression* > (); }
#line 1295 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 19:
#line 197 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    {
            yylhs.value.as< TBinaryOpExpression* > () = new (context) TBinaryOpExpression(
                context,
                yylhs.location,
                yystack_[1].value.as< EBinaryOp > (),
                yystack_[2].value.as< TExpression* > (),
                yystack_[0].value.as< TExpression* > ());
        }
#line 1308 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 20:
#line 209 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::Less; }
#line 1314 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 21:
#line 211 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::LessOrEqual; }
#line 1320 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 22:
#line 213 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::Equal; }
#line 1326 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 23:
#line 215 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::NotEqual; }
#line 1332 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 24:
#line 217 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::Greater; }
#line 1338 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;

  case 25:
#line 219 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:846
    { yylhs.value.as< EBinaryOp > () = EBinaryOp::GreaterOrEqual; }
#line 1344 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
    break;


#line 1348 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:846
          default:
            break;
          }
      }
    catch (const syntax_error& yyexc)
      {
        error (yyexc);
        YYERROR;
      }
    YY_SYMBOL_PRINT ("-> $$ =", yylhs);
    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();

    // Shift the result of the reduction.
    yypush_ (YY_NULL, yylhs);
    goto yynewstate;

  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        error (yyla.location, yysyntax_error_ (yystack_[0].state,
                                           yyempty ? yyempty_ : yyla.type_get ()));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.type_get () == yyeof_)
          YYABORT;
        else if (!yyempty)
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyempty = true;
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:

    /* Pacify compilers like GCC when the user code never invokes
       YYERROR and the label yyerrorlab therefore never appears in user
       code.  */
    if (false)
      goto yyerrorlab;
    yyerror_range[1].location = yystack_[yylen - 1].location;
    /* $$ was initialized before running the user action.  */
    YY_SYMBOL_PRINT ("Error: discarding", yylhs);
    yylhs.~stack_symbol_type();
    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    goto yyerrlab1;

  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    {
      stack_symbol_type error_token;
      for (;;)
        {
          yyn = yypact_[yystack_[0].state];
          if (!yy_pact_value_is_default_ (yyn))
            {
              yyn += yyterror_;
              if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == yyterror_)
                {
                  yyn = yytable_[yyn];
                  if (0 < yyn)
                    break;
                }
            }

          // Pop the current state because it cannot handle the error token.
          if (yystack_.size () == 1)
            YYABORT;

          yyerror_range[1].location = yystack_[0].location;
          yy_destroy_ ("Error: popping", yystack_[0]);
          yypop_ ();
          YY_STACK_PRINT ();
        }

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = yyn;
      yypush_ ("Shifting", error_token);
    }
    goto yynewstate;

    // Accept.
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;

    // Abort.
  yyabortlab:
    yyresult = 1;
    goto yyreturn;

  yyreturn:
    if (!yyempty)
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack"
                 << std::endl;
        // Do not try to display the values of the reclaimed symbols,
        // as their printer might throw an exception.
        if (!yyempty)
          yy_destroy_ (YY_NULL, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULL, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
  }

  void
  TParser::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what());
  }

  // Generate an error message.
  std::string
  TParser::yysyntax_error_ (state_type yystate, symbol_number_type yytoken) const
  {
    std::string yyres;
    // Number of reported tokens (one for the "unexpected", one per
    // "expected").
    size_t yycount = 0;
    // Its maximum.
    enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
    // Arguments of yyformat.
    char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];

    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yytoken) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state
         merging (from LALR or IELR) and default reductions corrupt the
         expected token list.  However, the list is correct for
         canonical LR with one exception: it will still contain any
         token that will not be accepted due to an error action in a
         later state.
    */
    if (yytoken != yyempty_)
      {
        yyarg[yycount++] = yytname_[yytoken];
        int yyn = yypact_[yystate];
        if (!yy_pact_value_is_default_ (yyn))
          {
            /* Start YYX at -YYN if negative to avoid negative indexes in
               YYCHECK.  In other words, skip the first -YYN actions for
               this state because they are default actions.  */
            int yyxbegin = yyn < 0 ? -yyn : 0;
            // Stay within bounds of both yycheck and yytname.
            int yychecklim = yylast_ - yyn + 1;
            int yyxend = yychecklim < yyntokens_ ? yychecklim : yyntokens_;
            for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
              if (yycheck_[yyx + yyn] == yyx && yyx != yyterror_
                  && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
                {
                  if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                    {
                      yycount = 1;
                      break;
                    }
                  else
                    yyarg[yycount++] = yytname_[yyx];
                }
          }
      }

    char const* yyformat = YY_NULL;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
        YYCASE_(0, YY_("syntax error"));
        YYCASE_(1, YY_("syntax error, unexpected %s"));
        YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    // Argument number.
    size_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += yytnamerr_ (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const signed char TParser::yypact_ninf_ = -13;

  const signed char TParser::yytable_ninf_ = -1;

  const signed char
  TParser::yypact_[] =
  {
      10,     2,   -13,   -13,     8,   -13,    -3,   -13,   -13,   -13,
      13,   -13,     5,    10,   -13,   -13,    17,   -13,   -13,    -2,
     -13,   -13,   -13,    13,   -13,    13,   -12,   -13,   -13,   -13,
     -13,   -13,   -13,   -13,   -13,    13,   -13
  };

  const unsigned char
  TParser::yydefact_[] =
  {
       0,    12,    13,    14,     0,     2,     0,     9,    10,    11,
       0,     1,     0,     0,     3,     4,     5,    12,    18,     0,
      17,     7,     8,     0,    15,     0,     0,     6,    16,    20,
      21,    22,    23,    24,    25,     0,    19
  };

  const signed char
  TParser::yypgoto_[] =
  {
     -13,   -13,   -13,   -13,   -13,   -13,   -13,    11,   -10,   -13,
     -13,     1,   -13,   -13
  };

  const signed char
  TParser::yydefgoto_[] =
  {
      -1,     4,     5,    14,    15,    16,     6,     7,     8,     9,
      19,    20,    27,    35
  };

  const unsigned char
  TParser::yytable_[] =
  {
      18,    12,    29,    30,    31,    32,    33,    34,    11,    24,
      13,    25,    10,    26,    21,    18,     1,     2,     3,    17,
       2,     3,    23,     0,    22,    36,    28
  };

  const signed char
  TParser::yycheck_[] =
  {
      10,     4,    14,    15,    16,    17,    18,    19,     0,    11,
      13,    13,    10,    23,     9,    25,     6,     7,     8,     6,
       7,     8,     5,    -1,    13,    35,    25
  };

  const unsigned char
  TParser::yystos_[] =
  {
       0,     6,     7,     8,    21,    22,    26,    27,    28,    29,
      10,     0,     4,    13,    23,    24,    25,     6,    28,    30,
      31,     9,    27,     5,    11,    13,    28,    32,    31,    14,
      15,    16,    17,    18,    19,    33,    28
  };

  const unsigned char
  TParser::yyr1_[] =
  {
       0,    20,    21,    22,    23,    24,    24,    25,    26,    26,
      27,    27,    28,    28,    28,    29,    30,    30,    31,    32,
      33,    33,    33,    33,    33,    33
  };

  const unsigned char
  TParser::yyr2_[] =
  {
       0,     2,     1,     2,     1,     1,     3,     2,     3,     1,
       1,     1,     1,     1,     1,     4,     3,     1,     1,     3,
       1,     1,     1,     1,     1,     1
  };



  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a yyntokens_, nonterminals.
  const char*
  const TParser::yytname_[] =
  {
  "\"end of stream\"", "error", "$undefined", "\"lexer failure\"",
  "\"keyword `FROM`\"", "\"keyword `WHERE`\"", "\"identifier\"",
  "\"integer literal\"", "\"double literal\"", "\"YPath literal\"",
  "\"`(`\"", "\"`)`\"", "\"`*`\"", "\"`,`\"", "\"`<`\"", "\"`<=`\"",
  "\"`=`\"", "\"`!=`\"", "\"`>`\"", "\"`>=`\"", "$accept", "head",
  "select-clause", "select-source", "from-where-clause", "from-clause",
  "projections", "projection", "atomic-expr", "function-expr",
  "function-expr-args", "function-expr-arg", "binary-rel-op-expr",
  "binary-rel-op", YY_NULL
  };

#if YT_QL_YYDEBUG
  const unsigned char
  TParser::yyrline_[] =
  {
       0,    85,    85,    92,   101,   106,   110,   119,   129,   134,
     141,   143,   148,   157,   161,   168,   179,   184,   191,   196,
     208,   210,   212,   214,   216,   218
  };

  // Print the state stack on the debug stream.
  void
  TParser::yystack_print_ ()
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << i->state;
    *yycdebug_ << std::endl;
  }

  // Report on the debug stream that the rule \a yyrule is going to be reduced.
  void
  TParser::yy_reduce_print_ (int yyrule)
  {
    unsigned int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):" << std::endl;
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YT_QL_YYDEBUG

  // Symbol number corresponding to token number t.
  inline
  TParser::token_number_type
  TParser::yytranslate_ (int t)
  {
    static
    const token_number_type
    translate_table[] =
    {
     0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      10,    11,    12,     2,    13,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      14,    16,    18,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     3,     1,     2,     4,
       5,     6,     7,     8,     9,    15,    17,    19
    };
    const unsigned int user_token_number_max_ = 267;
    const token_number_type undef_token_ = 2;

    if (static_cast<int>(t) <= yyeof_)
      return yyeof_;
    else if (static_cast<unsigned int> (t) <= user_token_number_max_)
      return translate_table[t];
    else
      return undef_token_;
  }

#line 5 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:1156
} } // NYT::NQueryClient
#line 1788 "/home/sandello/yt/source/yt/ytlib/query_client/parser.cpp" // lalr1.cc:1156
#line 222 "/home/sandello/yt/source/yt/ytlib/query_client/parser.yy" // lalr1.cc:1157


namespace NYT {
namespace NQueryClient {

const TSourceLocation NullSourceLocation = { 0, 0 };

void TParser::error(const location_type& location, const std::string& message)
{
    // TODO(sandello): Better diagnostics.
    std::cerr << message << std::endl;
}

} // namespace NQueryClient
} // namespace NYT

