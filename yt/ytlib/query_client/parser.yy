%skeleton "lalr1.cc"
%require "3.0"
%language "C++"

%define api.namespace {NYT::NQueryClient}
%define api.prefix {yt_ql_yy}
%define api.value.type variant
%define api.location.type {TSourceLocation}
%define parser_class_name {TParser}
%define parse.error verbose

%defines
%locations

%parse-param {TLexer& lexer}
%parse-param {TQueryContext* context}
%parse-param {const TOperator** head}

%code requires {
    #include <ytlib/query_client/ast.h>

    #include <cmath>
    #include <iostream>

    namespace NYT { namespace NQueryClient {
        class TLexer;
        class TParser;
    } }
}

%code {
    #include <ytlib/query_client/lexer.h>
    #define yt_ql_yylex lexer.GetNextToken
}

%token End 0 "end of stream"
%token Failure 256 "lexer failure"

// NB: Keep one-character tokens consistent with ASCII codes to simplify lexing.

%token KwFrom "keyword `FROM`"
%token KwWhere "keyword `WHERE`"

%token <TStringBuf> Identifier "identifier"

%token <i64> IntegerLiteral "integer literal"
%token <double> DoubleLiteral "double literal"
%token <TStringBuf> YPathLiteral "YPath literal"

%token LeftParenthesis 40 "`(`"
%token RightParenthesis 41 "`)`"

%token Asterisk 42 "`*`"
%token Comma 44 "`,`"

%token OpLess 60 "`<`"
%token OpLessOrEqual "`<=`"
%token OpEqual 61 "`=`"
%token OpNotEqual "`!=`"
%token OpGreater 62 "`>`"
%token OpGreaterOrEqual "`>=`"

%type <TOperator*> select-clause
%type <TOperator*> select-source
%type <TOperator*> from-where-clause
%type <TOperator*> from-clause

%type <TProjectOperator::TProjections> projections
%type <TExpression*> projection

%type <TExpression*> atomic-expr

%type <TFunctionExpression*> function-expr
%type <TFunctionExpression::TArguments> function-expr-args
%type <TExpression*> function-expr-arg

%type <TBinaryOpExpression*> binary-rel-op-expr
%type <EBinaryOp> binary-rel-op

%start head

%%

head
    : select-clause
        {
            *head = $[select-clause];
        }
;

select-clause
    : projections select-source[source]
        {
            auto projectOp = new (context) TProjectOperator(context, $source);
            projectOp->Projections().assign($projections.begin(), $projections.end());
            $$ = projectOp;
        }
;

select-source
    : from-where-clause
        { $$ = $1; }
;

from-where-clause
    : from-clause[source]
        {
            $$ = $source;
        }
    | from-clause[source] KwWhere binary-rel-op-expr[predicate]
        {
            auto filterOp = new (context) TFilterOperator(context, $source);
            filterOp->SetPredicate($predicate);
            $$ = filterOp;
        }
;

from-clause
    : KwFrom YPathLiteral[path]
        {
            auto tableIndex = context->GetTableIndexByAlias("");
            auto scanOp = new (context) TScanOperator(context, tableIndex);
            context->BindToTableIndex(tableIndex, $path, scanOp);
            $$ = scanOp;
        }
;

projections
    : projections[ps] Comma projection[p]
        {
            $$.swap($ps);
            $$.push_back($p);
        }
    | projection[p]
        {
            $$.push_back($p);
        }
;

projection
    : atomic-expr
        { $$ = $1; }
    | function-expr
        { $$ = $1; }
;

atomic-expr
    : Identifier[name]
        {
            auto tableIndex = context->GetTableIndexByAlias("");
            $$ = new (context) TReferenceExpression(
                context,
                @$,
                tableIndex,
                $name);
        }
    | IntegerLiteral[value]
        {
            $$ = new (context) TIntegerLiteralExpression(context, @$, $value);
        }
    | DoubleLiteral[value]
        {
            $$ = new (context) TDoubleLiteralExpression(context, @$, $value);
        }
;

function-expr
    : Identifier[name] LeftParenthesis function-expr-args[args] RightParenthesis
        {
            $$ = new (context) TFunctionExpression(
                context,
                @$,
                $name);
            $$->Arguments().assign($args.begin(), $args.end());
        }
;

function-expr-args
    : function-expr-args[as] Comma function-expr-arg[a]
        {
            $$.swap($as);
            $$.push_back($a);
        }
    | function-expr-arg[a]
        {
            $$.push_back($a);
        }
;

function-expr-arg
    : atomic-expr
        { $$ = $1; }
;

binary-rel-op-expr
    : atomic-expr[lhs] binary-rel-op[opcode] atomic-expr[rhs]
        {
            $$ = new (context) TBinaryOpExpression(
                context,
                @$,
                $opcode,
                $lhs,
                $rhs);
        }
;

binary-rel-op
    : OpLess
        { $$ = EBinaryOp::Less; }
    | OpLessOrEqual
        { $$ = EBinaryOp::LessOrEqual; }
    | OpEqual
        { $$ = EBinaryOp::Equal; }
    | OpNotEqual
        { $$ = EBinaryOp::NotEqual; }
    | OpGreater
        { $$ = EBinaryOp::Greater; }
    | OpGreaterOrEqual
        { $$ = EBinaryOp::GreaterOrEqual; }
;

%%

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

