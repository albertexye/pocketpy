#pragma once

#include "codeobject.h"
#include "common.h"
#include "expr.h"
#include "obj.h"

namespace pkpy{

class Compiler;
typedef void (Compiler::*PrattCallback)();

struct PrattRule{
    PrattCallback prefix;
    PrattCallback infix;
    Precedence precedence;
};

class Compiler {
    inline static PrattRule rules[kTokenCount];
    std::unique_ptr<Lexer> lexer;
    stack<CodeEmitContext> contexts;
    VM* vm;
    bool unknown_global_scope;     // for eval/exec() call
    bool used;
    // for parsing token stream
    int i = 0;
    std::vector<Token> tokens;

    const Token& prev() const{ return tokens.at(i-1); }
    const Token& curr() const{ return tokens.at(i); }
    const Token& next() const{ return tokens.at(i+1); }
    const Token& err() const{
        if(i >= tokens.size()) return prev();
        return curr();
    }
    void advance(int delta=1) { i += delta; }

    CodeEmitContext* ctx() { return &contexts.top(); }
    CompileMode mode() const{ return lexer->src->mode; }
    NameScope name_scope() const {
        auto s = contexts.size()>1 ? NAME_LOCAL : NAME_GLOBAL;
        if(unknown_global_scope && s == NAME_GLOBAL) s = NAME_GLOBAL_UNKNOWN;
        return s;
    }

    CodeObject_ push_global_context(){
        CodeObject_ co = make_sp<CodeObject>(lexer->src, lexer->src->filename);
        contexts.push(CodeEmitContext(vm, co, contexts.size()));
        return co;
    }

    FuncDecl_ push_f_context(Str name){
        FuncDecl_ decl = make_sp<FuncDecl>();
        decl->code = make_sp<CodeObject>(lexer->src, name);
        decl->nested = name_scope() == NAME_LOCAL;
        contexts.push(CodeEmitContext(vm, decl->code, contexts.size()));
        return decl;
    }

    void pop_context(){
        if(!ctx()->s_expr.empty()){
            throw std::runtime_error("!ctx()->s_expr.empty()\n" + ctx()->_log_s_expr());
        }
        // if the last op does not return, add a default return None
        if(ctx()->co->codes.empty() || ctx()->co->codes.back().op != OP_RETURN_VALUE){
            ctx()->emit(OP_LOAD_NONE, BC_NOARG, BC_KEEPLINE);
            ctx()->emit(OP_RETURN_VALUE, BC_NOARG, BC_KEEPLINE);
        }
        ctx()->co->optimize(vm);
        contexts.pop();
    }

    static void init_pratt_rules(){
        if(rules[TK(".")].precedence != PREC_NONE) return;
// http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/
#define METHOD(name) &Compiler::name
#define NO_INFIX nullptr, PREC_NONE
        for(TokenIndex i=0; i<kTokenCount; i++) rules[i] = { nullptr, NO_INFIX };
        rules[TK(".")] =        { nullptr,               METHOD(exprAttrib),         PREC_ATTRIB };
        rules[TK("(")] =        { METHOD(exprGroup),     METHOD(exprCall),           PREC_CALL };
        rules[TK("[")] =        { METHOD(exprList),      METHOD(exprSubscr),         PREC_SUBSCRIPT };
        rules[TK("{")] =        { METHOD(exprMap),       NO_INFIX };
        rules[TK("%")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_FACTOR };
        rules[TK("+")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_TERM };
        rules[TK("-")] =        { METHOD(exprUnaryOp),   METHOD(exprBinaryOp),       PREC_TERM };
        rules[TK("*")] =        { METHOD(exprUnaryOp),   METHOD(exprBinaryOp),       PREC_FACTOR };
        rules[TK("/")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_FACTOR };
        rules[TK("//")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_FACTOR };
        rules[TK("**")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_EXPONENT };
        rules[TK(">")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_COMPARISION };
        rules[TK("<")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_COMPARISION };
        rules[TK("==")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_EQUALITY };
        rules[TK("!=")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_EQUALITY };
        rules[TK(">=")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_COMPARISION };
        rules[TK("<=")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_COMPARISION };
        rules[TK("in")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_TEST };
        rules[TK("is")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_TEST };
        rules[TK("<<")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_BITWISE_SHIFT };
        rules[TK(">>")] =       { nullptr,               METHOD(exprBinaryOp),       PREC_BITWISE_SHIFT };
        rules[TK("&")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_BITWISE_AND };
        rules[TK("|")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_BITWISE_OR };
        rules[TK("^")] =        { nullptr,               METHOD(exprBinaryOp),       PREC_BITWISE_XOR };
        rules[TK("?")] =        { nullptr,               METHOD(exprTernary),        PREC_TERNARY };
        rules[TK("if")] =       { nullptr,               METHOD(exprTernary),        PREC_TERNARY };
        rules[TK(",")] =        { nullptr,               METHOD(exprTuple),          PREC_TUPLE };
        rules[TK("not in")] =   { nullptr,               METHOD(exprBinaryOp),       PREC_TEST };
        rules[TK("is not")] =   { nullptr,               METHOD(exprBinaryOp),       PREC_TEST };
        rules[TK("and") ] =     { nullptr,               METHOD(exprAnd),            PREC_LOGICAL_AND };
        rules[TK("or")] =       { nullptr,               METHOD(exprOr),             PREC_LOGICAL_OR };
        rules[TK("not")] =      { METHOD(exprNot),       nullptr,                    PREC_LOGICAL_NOT };
        rules[TK("True")] =     { METHOD(exprLiteral0),  NO_INFIX };
        rules[TK("False")] =    { METHOD(exprLiteral0),  NO_INFIX };
        rules[TK("None")] =     { METHOD(exprLiteral0),  NO_INFIX };
        rules[TK("...")] =      { METHOD(exprLiteral0),  NO_INFIX };
        rules[TK("lambda")] =   { METHOD(exprLambda),    NO_INFIX };
        rules[TK("@id")] =      { METHOD(exprName),      NO_INFIX };
        rules[TK("@num")] =     { METHOD(exprLiteral),   NO_INFIX };
        rules[TK("@str")] =     { METHOD(exprLiteral),   NO_INFIX };
        rules[TK("@fstr")] =    { METHOD(exprFString),   NO_INFIX };
#undef METHOD
#undef NO_INFIX
    }

    bool match(TokenIndex expected) {
        if (curr().type != expected) return false;
        advance();
        return true;
    }

    void consume(TokenIndex expected) {
        if (!match(expected)){
            SyntaxError(
                fmt("expected '", TK_STR(expected), "', but got '", TK_STR(curr().type), "'")
            );
        }
    }

    bool match_newlines_repl(){
        return match_newlines(mode()==REPL_MODE);
    }

    bool match_newlines(bool repl_throw=false) {
        bool consumed = false;
        if (curr().type == TK("@eol")) {
            while (curr().type == TK("@eol")) advance();
            consumed = true;
        }
        if (repl_throw && curr().type == TK("@eof")){
            throw NeedMoreLines(ctx()->is_compiling_class);
        }
        return consumed;
    }

    bool match_end_stmt() {
        if (match(TK(";"))) { match_newlines(); return true; }
        if (match_newlines() || curr().type == TK("@eof")) return true;
        if (curr().type == TK("@dedent")) return true;
        return false;
    }

    void consume_end_stmt() {
        if (!match_end_stmt()) SyntaxError("expected statement end");
    }

    /*************************************************/

    void EXPR(bool push_stack=true) {
        parse_expression(PREC_TUPLE+1, push_stack);
    }

    void EXPR_TUPLE(bool push_stack=true) {
        parse_expression(PREC_TUPLE, push_stack);
    }

    // special case for `for loop` and `comp`
    Expr_ EXPR_VARS(){
        std::vector<Expr_> items;
        do {
            consume(TK("@id"));
            items.push_back(make_expr<NameExpr>(prev().str(), name_scope()));
        } while(match(TK(",")));
        if(items.size()==1) return std::move(items[0]);
        return make_expr<TupleExpr>(std::move(items));
    }

    template <typename T, typename... Args>
    std::unique_ptr<T> make_expr(Args&&... args) {
        std::unique_ptr<T> expr = std::make_unique<T>(std::forward<Args>(args)...);
        expr->line = prev().line;
        return expr;
    }

    
    void exprLiteral(){
        ctx()->s_expr.push(make_expr<LiteralExpr>(prev().value));
    }

    
    void exprFString(){
        ctx()->s_expr.push(make_expr<FStringExpr>(std::get<Str>(prev().value)));
    }

    
    void exprLambda(){
        FuncDecl_ decl = push_f_context("<lambda>");
        auto e = make_expr<LambdaExpr>(decl);
        if(!match(TK(":"))){
            _compile_f_args(e->decl, false);
            consume(TK(":"));
        }
        // https://github.com/blueloveTH/pocketpy/issues/37
        parse_expression(PREC_LAMBDA + 1, false);
        ctx()->emit(OP_RETURN_VALUE, BC_NOARG, BC_KEEPLINE);
        pop_context();
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprTuple(){
        std::vector<Expr_> items;
        items.push_back(ctx()->s_expr.popx());
        do {
            EXPR();         // NOTE: "1," will fail, "1,2" will be ok
            items.push_back(ctx()->s_expr.popx());
        } while(match(TK(",")));
        ctx()->s_expr.push(make_expr<TupleExpr>(
            std::move(items)
        ));
    }

    
    void exprOr(){
        auto e = make_expr<OrExpr>();
        e->lhs = ctx()->s_expr.popx();
        parse_expression(PREC_LOGICAL_OR + 1);
        e->rhs = ctx()->s_expr.popx();
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprAnd(){
        auto e = make_expr<AndExpr>();
        e->lhs = ctx()->s_expr.popx();
        parse_expression(PREC_LOGICAL_AND + 1);
        e->rhs = ctx()->s_expr.popx();
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprTernary(){
        auto e = make_expr<TernaryExpr>();
        if(prev().type == TK("if")){
            e->true_expr = ctx()->s_expr.popx();
            // cond
            parse_expression(PREC_TERNARY + 1);
            e->cond = ctx()->s_expr.popx();
            consume(TK("else"));
            // if false
            parse_expression(PREC_TERNARY + 1);
            e->false_expr = ctx()->s_expr.popx();
        }else{  // ?:
            e->cond = ctx()->s_expr.popx();
            // if true
            parse_expression(PREC_TERNARY + 1);
            e->true_expr = ctx()->s_expr.popx();
            consume(TK(":"));
            // if false
            parse_expression(PREC_TERNARY + 1);
            e->false_expr = ctx()->s_expr.popx();
        }
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprBinaryOp(){
        auto e = make_expr<BinaryExpr>();
        e->op = prev().type;
        e->lhs = ctx()->s_expr.popx();
        parse_expression(rules[e->op].precedence + 1);
        e->rhs = ctx()->s_expr.popx();
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprNot() {
        parse_expression(PREC_LOGICAL_NOT + 1);
        ctx()->s_expr.push(make_expr<NotExpr>(ctx()->s_expr.popx()));
    }

    
    void exprUnaryOp(){
        TokenIndex op = prev().type;
        parse_expression(PREC_UNARY + 1);
        switch(op){
            case TK("-"):
                ctx()->s_expr.push(make_expr<NegatedExpr>(ctx()->s_expr.popx()));
                break;
            case TK("*"):
                ctx()->s_expr.push(make_expr<StarredExpr>(ctx()->s_expr.popx()));
                break;
            default: FATAL_ERROR();
        }
    }

    
    void exprGroup(){
        match_newlines_repl();
        EXPR_TUPLE();   // () is just for change precedence
        match_newlines_repl();
        consume(TK(")"));
    }

    
    template<typename T>
    void _consume_comp(Expr_ expr){
        static_assert(std::is_base_of<CompExpr, T>::value);
        std::unique_ptr<CompExpr> ce = make_expr<T>();
        ce->expr = std::move(expr);
        ce->vars = EXPR_VARS();
        consume(TK("in"));
        parse_expression(PREC_TERNARY + 1);
        ce->iter = ctx()->s_expr.popx();
        match_newlines_repl();
        if(match(TK("if"))){
            parse_expression(PREC_TERNARY + 1);
            ce->cond = ctx()->s_expr.popx();
        }
        ctx()->s_expr.push(std::move(ce));
        match_newlines_repl();
    }

    
    void exprList() {
        int line = prev().line;
        std::vector<Expr_> items;
        do {
            match_newlines_repl();
            if (curr().type == TK("]")) break;
            EXPR();
            items.push_back(ctx()->s_expr.popx());
            match_newlines_repl();
            if(items.size()==1 && match(TK("for"))){
                _consume_comp<ListCompExpr>(std::move(items[0]));
                consume(TK("]"));
                return;
            }
            match_newlines_repl();
        } while (match(TK(",")));
        consume(TK("]"));
        auto e = make_expr<ListExpr>(std::move(items));
        e->line = line;     // override line
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprMap() {
        bool parsing_dict = false;  // {...} may be dict or set
        std::vector<Expr_> items;
        do {
            match_newlines_repl();
            if (curr().type == TK("}")) break;
            EXPR();
            if(curr().type == TK(":")) parsing_dict = true;
            if(parsing_dict){
                consume(TK(":"));
                EXPR();
                auto dict_item = make_expr<DictItemExpr>();
                dict_item->key = ctx()->s_expr.popx();
                dict_item->value = ctx()->s_expr.popx();
                items.push_back(std::move(dict_item));
            }else{
                items.push_back(ctx()->s_expr.popx());
            }
            match_newlines_repl();
            if(items.size()==1 && match(TK("for"))){
                if(parsing_dict) _consume_comp<DictCompExpr>(std::move(items[0]));
                else _consume_comp<SetCompExpr>(std::move(items[0]));
                consume(TK("}"));
                return;
            }
            match_newlines_repl();
        } while (match(TK(",")));
        consume(TK("}"));
        if(items.size()==0 || parsing_dict){
            auto e = make_expr<DictExpr>(std::move(items));
            ctx()->s_expr.push(std::move(e));
        }else{
            auto e = make_expr<SetExpr>(std::move(items));
            ctx()->s_expr.push(std::move(e));
        }
    }

    
    void exprCall() {
        auto e = make_expr<CallExpr>();
        e->callable = ctx()->s_expr.popx();
        do {
            match_newlines_repl();
            if (curr().type==TK(")")) break;
            if(curr().type==TK("@id") && next().type==TK("=")) {
                consume(TK("@id"));
                Str key = prev().str();
                consume(TK("="));
                EXPR();
                e->kwargs.push_back({key, ctx()->s_expr.popx()});
            } else{
                if(!e->kwargs.empty()) SyntaxError("positional argument follows keyword argument");
                EXPR();
                e->args.push_back(ctx()->s_expr.popx());
            }
            match_newlines_repl();
        } while (match(TK(",")));
        consume(TK(")"));
        if(e->args.size() > 32767) SyntaxError("too many positional arguments");
        if(e->kwargs.size() > 32767) SyntaxError("too many keyword arguments");
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprName(){
        Str name = prev().str();
        NameScope scope = name_scope();
        if(ctx()->co->global_names.count(name)){
            scope = NAME_GLOBAL;
        }
        ctx()->s_expr.push(make_expr<NameExpr>(name, scope));
    }

    
    void exprAttrib() {
        consume(TK("@id"));
        ctx()->s_expr.push(
            make_expr<AttribExpr>(ctx()->s_expr.popx(), prev().str())
        );
    }

    
    void exprSubscr() {
        auto e = make_expr<SubscrExpr>();
        e->a = ctx()->s_expr.popx();
        auto slice = make_expr<SliceExpr>();
        bool is_slice = false;
        // a[<0> <state:1> : state<3> : state<5>]
        int state = 0;
        do{
            switch(state){
                case 0:
                    if(match(TK(":"))){
                        is_slice=true;
                        state=2;
                        break;
                    }
                    if(match(TK("]"))) SyntaxError();
                    EXPR_TUPLE();
                    slice->start = ctx()->s_expr.popx();
                    state=1;
                    break;
                case 1:
                    if(match(TK(":"))){
                        is_slice=true;
                        state=2;
                        break;
                    }
                    if(match(TK("]"))) goto __SUBSCR_END;
                    SyntaxError("expected ':' or ']'");
                    break;
                case 2:
                    if(match(TK(":"))){
                        state=4;
                        break;
                    }
                    if(match(TK("]"))) goto __SUBSCR_END;
                    EXPR_TUPLE();
                    slice->stop = ctx()->s_expr.popx();
                    state=3;
                    break;
                case 3:
                    if(match(TK(":"))){
                        state=4;
                        break;
                    }
                    if(match(TK("]"))) goto __SUBSCR_END;
                    SyntaxError("expected ':' or ']'");
                    break;
                case 4:
                    if(match(TK("]"))) goto __SUBSCR_END;
                    EXPR_TUPLE();
                    slice->step = ctx()->s_expr.popx();
                    state=5;
                    break;
                case 5: consume(TK("]")); goto __SUBSCR_END;
            }
        }while(true);
__SUBSCR_END:
        if(is_slice){
            e->b = std::move(slice);
        }else{
            if(state != 1) FATAL_ERROR();
            e->b = std::move(slice->start);
        }
        ctx()->s_expr.push(std::move(e));
    }

    
    void exprLiteral0() {
        ctx()->s_expr.push(make_expr<Literal0Expr>(prev().type));
    }

    void compile_block_body() {
        consume(TK(":"));
        if(curr().type!=TK("@eol") && curr().type!=TK("@eof")){
            compile_stmt();     // inline block
            return;
        }
        if(!match_newlines(mode()==REPL_MODE)){
            SyntaxError("expected a new line after ':'");
        }
        consume(TK("@indent"));
        while (curr().type != TK("@dedent")) {
            match_newlines();
            compile_stmt();
            match_newlines();
        }
        consume(TK("@dedent"));
    }

    Str _compile_import() {
        if(name_scope() != NAME_GLOBAL) SyntaxError("import statement should be used in global scope");
        consume(TK("@id"));
        Str name = prev().str();
        ctx()->emit(OP_IMPORT_NAME, StrName(name).index, prev().line);
        return name;
    }

    // import a as b
    void compile_normal_import() {
        do {
            Str name = _compile_import();
            if (match(TK("as"))) {
                consume(TK("@id"));
                name = prev().str();
            }
            ctx()->emit(OP_STORE_GLOBAL, StrName(name).index, prev().line);
        } while (match(TK(",")));
        consume_end_stmt();
    }

    // from a import b as c, d as e
    void compile_from_import() {
        _compile_import();
        consume(TK("import"));
        if (match(TK("*"))) {
            ctx()->emit(OP_IMPORT_STAR, BC_NOARG, prev().line);
            consume_end_stmt();
            return;
        }
        do {
            ctx()->emit(OP_DUP_TOP, BC_NOARG, BC_KEEPLINE);
            consume(TK("@id"));
            Str name = prev().str();
            ctx()->emit(OP_LOAD_ATTR, StrName(name).index, prev().line);
            if (match(TK("as"))) {
                consume(TK("@id"));
                name = prev().str();
            }
            ctx()->emit(OP_STORE_GLOBAL, StrName(name).index, prev().line);
        } while (match(TK(",")));
        ctx()->emit(OP_POP_TOP, BC_NOARG, BC_KEEPLINE);
        consume_end_stmt();
    }

    void parse_expression(int precedence, bool push_stack=true) {
        advance();
        PrattCallback prefix = rules[prev().type].prefix;
        if (prefix == nullptr) SyntaxError(Str("expected an expression, but got ") + TK_STR(prev().type));
        (this->*prefix)();
        while (rules[curr().type].precedence >= precedence) {
            TokenIndex op = curr().type;
            advance();
            PrattCallback infix = rules[op].infix;
            if(infix == nullptr) throw std::runtime_error("(infix == nullptr) is true");
            (this->*infix)();
        }
        if(!push_stack) ctx()->emit_expr();
    }

    
    void compile_if_stmt() {
        EXPR(false);   // condition
        int patch = ctx()->emit(OP_POP_JUMP_IF_FALSE, BC_NOARG, prev().line);
        compile_block_body();
        if (match(TK("elif"))) {
            int exit_patch = ctx()->emit(OP_JUMP_ABSOLUTE, BC_NOARG, prev().line);
            ctx()->patch_jump(patch);
            compile_if_stmt();
            ctx()->patch_jump(exit_patch);
        } else if (match(TK("else"))) {
            int exit_patch = ctx()->emit(OP_JUMP_ABSOLUTE, BC_NOARG, prev().line);
            ctx()->patch_jump(patch);
            compile_block_body();
            ctx()->patch_jump(exit_patch);
        } else {
            ctx()->patch_jump(patch);
        }
    }

    
    void compile_while_loop() {
        ctx()->enter_block(WHILE_LOOP);
        EXPR(false);   // condition
        int patch = ctx()->emit(OP_POP_JUMP_IF_FALSE, BC_NOARG, prev().line);
        compile_block_body();
        ctx()->emit(OP_LOOP_CONTINUE, BC_NOARG, BC_KEEPLINE);
        ctx()->patch_jump(patch);
        ctx()->exit_block();
    }

    
    void compile_for_loop() {
        Expr_ vars = EXPR_VARS();
        consume(TK("in"));
        EXPR(false);
        ctx()->emit(OP_GET_ITER, BC_NOARG, BC_KEEPLINE);
        ctx()->enter_block(FOR_LOOP);
        ctx()->emit(OP_FOR_ITER, BC_NOARG, BC_KEEPLINE);
        bool ok = vars->emit_store(ctx());
        if(!ok) SyntaxError();  // this error occurs in `vars` instead of this line, but...nevermind
        compile_block_body();
        ctx()->emit(OP_LOOP_CONTINUE, BC_NOARG, BC_KEEPLINE);
        ctx()->exit_block();
    }

    void compile_try_except() {
        ctx()->enter_block(TRY_EXCEPT);
        compile_block_body();
        std::vector<int> patches = {
            ctx()->emit(OP_JUMP_ABSOLUTE, BC_NOARG, BC_KEEPLINE)
        };
        ctx()->exit_block();
        do {
            consume(TK("except"));
            if(match(TK("@id"))){
                ctx()->emit(OP_EXCEPTION_MATCH, StrName(prev().str()).index, prev().line);
            }else{
                ctx()->emit(OP_LOAD_TRUE, BC_NOARG, BC_KEEPLINE);
            }
            int patch = ctx()->emit(OP_POP_JUMP_IF_FALSE, BC_NOARG, BC_KEEPLINE);
            // pop the exception on match
            ctx()->emit(OP_POP_TOP, BC_NOARG, BC_KEEPLINE);
            compile_block_body();
            patches.push_back(ctx()->emit(OP_JUMP_ABSOLUTE, BC_NOARG, BC_KEEPLINE));
            ctx()->patch_jump(patch);
        }while(curr().type == TK("except"));
        // no match, re-raise
        ctx()->emit(OP_RE_RAISE, BC_NOARG, BC_KEEPLINE);
        for (int patch : patches) ctx()->patch_jump(patch);
    }

    void compile_decorated(){
        std::vector<Expr_> decorators;
        do{
            EXPR();
            decorators.push_back(ctx()->s_expr.popx());
            if(!match_newlines_repl()) SyntaxError();
        }while(match(TK("@")));
        consume(TK("def"));
        compile_function(decorators);
    }

    bool try_compile_assignment(){
        Expr* lhs_p = ctx()->s_expr.top().get();
        bool inplace;
        switch (curr().type) {
            case TK("+="): case TK("-="): case TK("*="): case TK("/="): case TK("//="): case TK("%="):
            case TK("<<="): case TK(">>="): case TK("&="): case TK("|="): case TK("^="): {
                if(ctx()->is_compiling_class) SyntaxError();
                inplace = true;
                advance();
                auto e = make_expr<BinaryExpr>();
                e->op = prev().type - 1; // -1 to remove =
                e->lhs = ctx()->s_expr.popx();
                EXPR_TUPLE();
                e->rhs = ctx()->s_expr.popx();
                ctx()->s_expr.push(std::move(e));
            } break;
            case TK("="):
                inplace = false;
                advance();
                EXPR_TUPLE();
                break;
            default: return false;
        }
        // std::cout << ctx()->_log_s_expr() << std::endl;
        Expr_ rhs = ctx()->s_expr.popx();

        if(lhs_p->is_starred() || rhs->is_starred()){
            SyntaxError("can't use starred expression here");
        }

        rhs->emit(ctx());
        bool ok = lhs_p->emit_store(ctx());
        if(!ok) SyntaxError();
        if(!inplace) ctx()->s_expr.pop();
        return true;
    }

    void compile_stmt() {
        advance();
        int kw_line = prev().line;  // backup line number
        switch(prev().type){
            case TK("break"):
                if (!ctx()->is_curr_block_loop()) SyntaxError("'break' outside loop");
                ctx()->emit(OP_LOOP_BREAK, BC_NOARG, kw_line);
                consume_end_stmt();
                break;
            case TK("continue"):
                if (!ctx()->is_curr_block_loop()) SyntaxError("'continue' not properly in loop");
                ctx()->emit(OP_LOOP_CONTINUE, BC_NOARG, kw_line);
                consume_end_stmt();
                break;
            case TK("yield"): 
                if (contexts.size() <= 1) SyntaxError("'yield' outside function");
                EXPR_TUPLE(false);
                // if yield present, mark the function as generator
                ctx()->co->is_generator = true;
                ctx()->emit(OP_YIELD_VALUE, BC_NOARG, kw_line);
                consume_end_stmt();
                break;
            case TK("return"):
                if (contexts.size() <= 1) SyntaxError("'return' outside function");
                if(match_end_stmt()){
                    ctx()->emit(OP_LOAD_NONE, BC_NOARG, kw_line);
                }else{
                    EXPR_TUPLE(false);
                    consume_end_stmt();
                }
                ctx()->emit(OP_RETURN_VALUE, BC_NOARG, kw_line);
                break;
            /*************************************************/
            case TK("if"): compile_if_stmt(); break;
            case TK("while"): compile_while_loop(); break;
            case TK("for"): compile_for_loop(); break;
            case TK("import"): compile_normal_import(); break;
            case TK("from"): compile_from_import(); break;
            case TK("def"): compile_function(); break;
            case TK("@"): compile_decorated(); break;
            case TK("try"): compile_try_except(); break;
            case TK("pass"): consume_end_stmt(); break;
            /*************************************************/
            case TK("assert"):
                EXPR_TUPLE(false);
                ctx()->emit(OP_ASSERT, BC_NOARG, kw_line);
                consume_end_stmt();
                break;
            case TK("global"):
                do {
                    consume(TK("@id"));
                    ctx()->co->global_names.insert(prev().str());
                } while (match(TK(",")));
                consume_end_stmt();
                break;
            case TK("raise"): {
                consume(TK("@id"));
                int dummy_t = StrName(prev().str()).index;
                if(match(TK("(")) && !match(TK(")"))){
                    EXPR(false); consume(TK(")"));
                }else{
                    ctx()->emit(OP_LOAD_NONE, BC_NOARG, BC_KEEPLINE);
                }
                ctx()->emit(OP_RAISE, dummy_t, kw_line);
                consume_end_stmt();
            } break;
            case TK("del"): {
                EXPR_TUPLE();
                Expr_ e = ctx()->s_expr.popx();
                bool ok = e->emit_del(ctx());
                if(!ok) SyntaxError();
                consume_end_stmt();
            } break;
            case TK("with"): {
                // TODO: reimpl this
                EXPR(false);
                ctx()->emit(OP_POP_TOP, BC_NOARG, prev().line);
                consume(TK("as"));
                consume(TK("@id"));
                // emit(OP_STORE_NAME, index);
                // emit(OP_LOAD_NAME_REF, index);
                // emit(OP_WITH_ENTER);
                compile_block_body();
                // emit(OP_LOAD_NAME_REF, index);
                // emit(OP_WITH_EXIT);
            } break;
            /*************************************************/
            // TODO: refactor goto/label use special $ syntax
            case TK("label"): {
                if(mode()!=EXEC_MODE) SyntaxError("'label' is only available in EXEC_MODE");
                consume(TK(".")); consume(TK("@id"));
                bool ok = ctx()->add_label(prev().str());
                if(!ok) SyntaxError("label " + prev().str().escape() + " already exists");
                consume_end_stmt();
            } break;
            case TK("goto"):
                if(mode()!=EXEC_MODE) SyntaxError("'goto' is only available in EXEC_MODE");
                consume(TK(".")); consume(TK("@id"));
                ctx()->emit(OP_GOTO, StrName(prev().str()).index, prev().line);
                consume_end_stmt();
                break;
            /*************************************************/
            // handle dangling expression or assignment
            default: {
                advance(-1);    // do revert since we have pre-called advance() at the beginning
                EXPR_TUPLE();
                if(!try_compile_assignment()){
                    ctx()->emit_expr();
                    if(mode()==REPL_MODE && name_scope()==NAME_GLOBAL){
                        ctx()->emit(OP_PRINT_EXPR, BC_NOARG, BC_KEEPLINE);
                    }else{
                        ctx()->emit(OP_POP_TOP, BC_NOARG, BC_KEEPLINE);
                    }
                }
                consume_end_stmt();
            }
        }
    }

    
    void compile_class(){
        consume(TK("@id"));
        int namei = StrName(prev().str()).index;
        int super_namei = -1;
        if(match(TK("(")) && match(TK("@id"))){
            super_namei = StrName(prev().str()).index;
            consume(TK(")"));
        }
        if(super_namei == -1) ctx()->emit(OP_LOAD_NONE, BC_NOARG, prev().line);
        else ctx()->emit(OP_LOAD_GLOBAL, super_namei, prev().line);
        ctx()->emit(OP_BEGIN_CLASS, namei, BC_KEEPLINE);
        ctx()->is_compiling_class = true;
        compile_block_body();
        ctx()->is_compiling_class = false;
        ctx()->emit(OP_END_CLASS, BC_NOARG, BC_KEEPLINE);
    }

    void _compile_f_args(FuncDecl_ decl, bool enable_type_hints){
        int state = 0;      // 0 for args, 1 for *args, 2 for k=v, 3 for **kwargs
        do {
            if(state == 3) SyntaxError("**kwargs should be the last argument");
            match_newlines();
            if(match(TK("*"))){
                if(state < 1) state = 1;
                else SyntaxError("*args should be placed before **kwargs");
            }
            else if(match(TK("**"))){
                state = 3;
            }
            consume(TK("@id"));
            StrName name = prev().str();

            // check duplicate argument name
            for(int i: decl->args){
                if(decl->code->varnames[i] == name) {
                    SyntaxError("duplicate argument name");
                }
            }
            if(decl->starred_arg!=-1 && decl->code->varnames[decl->starred_arg] == name){
                SyntaxError("duplicate argument name");
            }
            for(auto& kv: decl->kwargs){
                if(decl->code->varnames[kv.key] == name){
                    SyntaxError("duplicate argument name");
                }
            }

            // eat type hints
            if(enable_type_hints && match(TK(":"))) consume(TK("@id"));
            if(state == 0 && curr().type == TK("=")) state = 2;
            int index = ctx()->add_varname(name);
            switch (state)
            {
                case 0:
                    decl->args.push_back(index);
                    break;
                case 1:
                    decl->starred_arg = index;
                    state+=1;
                    break;
                case 2: {
                    consume(TK("="));
                    PyObject* value = read_literal();
                    if(value == nullptr){
                        SyntaxError(Str("expect a literal, not ") + TK_STR(curr().type));
                    }
                    decl->kwargs.push_back(FuncDecl::KwArg{index, value});
                } break;
                case 3: SyntaxError("**kwargs is not supported yet"); break;
            }
        } while (match(TK(",")));
    }

    void compile_function(const std::vector<Expr_>& decorators={}){
        Str obj_name;
        Str decl_name;
        consume(TK("@id"));
        decl_name = prev().str();
        if(!ctx()->is_compiling_class && match(TK("::"))){
            consume(TK("@id"));
            obj_name = decl_name;
            decl_name = prev().str();
        }
        FuncDecl_ decl = push_f_context(decl_name);
        consume(TK("("));
        if (!match(TK(")"))) {
            _compile_f_args(decl, true);
            consume(TK(")"));
        }
        if(match(TK("->"))){
            if(!match(TK("None"))) consume(TK("@id"));
        }
        compile_block_body();
        pop_context();
        ctx()->emit(OP_LOAD_FUNCTION, ctx()->add_func_decl(decl), prev().line);
        // add decorators
        for(auto it=decorators.rbegin(); it!=decorators.rend(); ++it){
            (*it)->emit(ctx());
            ctx()->emit(OP_ROT_TWO, BC_NOARG, (*it)->line);
            ctx()->emit(OP_LOAD_NULL, BC_NOARG, BC_KEEPLINE);
            ctx()->emit(OP_ROT_TWO, BC_NOARG, BC_KEEPLINE);
            ctx()->emit(OP_CALL, 1, (*it)->line);
        }
        if(!ctx()->is_compiling_class){
            if(obj_name.empty()){
                auto e = make_expr<NameExpr>(decl_name, name_scope());
                e->emit_store(ctx());
            } else {
                ctx()->emit(OP_LOAD_GLOBAL, StrName(obj_name).index, prev().line);
                int index = StrName(decl_name).index;
                ctx()->emit(OP_STORE_ATTR, index, prev().line);
            }
        }else{
            int index = StrName(decl_name).index;
            ctx()->emit(OP_STORE_CLASS_ATTR, index, prev().line);
        }
    }

    PyObject* to_object(const TokenValue& value){
        PyObject* obj = nullptr;
        if(std::holds_alternative<i64>(value)){
            obj = VAR(std::get<i64>(value));
        }
        if(std::holds_alternative<f64>(value)){
            obj = VAR(std::get<f64>(value));
        }
        if(std::holds_alternative<Str>(value)){
            obj = VAR(std::get<Str>(value));
        }
        if(obj == nullptr) FATAL_ERROR();
        return obj;
    }

    PyObject* read_literal(){
        advance();
        switch(prev().type){
            case TK("-"): {
                consume(TK("@num"));
                PyObject* val = to_object(prev().value);
                return vm->num_negated(val);
            }
            case TK("@num"): return to_object(prev().value);
            case TK("@str"): return to_object(prev().value);
            case TK("True"): return VAR(true);
            case TK("False"): return VAR(false);
            case TK("None"): return vm->None;
            case TK("..."): return vm->Ellipsis;
            default: break;
        }
        return nullptr;
    }

    void SyntaxError(Str msg){ lexer->throw_err("SyntaxError", msg, err().line, err().start); }
    void SyntaxError(){ lexer->throw_err("SyntaxError", "invalid syntax", err().line, err().start); }
    void IndentationError(Str msg){ lexer->throw_err("IndentationError", msg, err().line, err().start); }

public:
    Compiler(VM* vm, const Str& source, const Str& filename, CompileMode mode, bool unknown_global_scope=false){
        this->vm = vm;
        this->used = false;
        this->unknown_global_scope = unknown_global_scope;
        this->lexer = std::make_unique<Lexer>(
            make_sp<SourceData>(source, filename, mode)
        );
        init_pratt_rules();
    }

    CodeObject_ compile(){
        if(used) FATAL_ERROR();
        used = true;

        tokens = lexer->run();
        // if(lexer->src->filename == "<stdin>"){
        //     for(auto& t: tokens) std::cout << t.info() << std::endl;
        // }

        CodeObject_ code = push_global_context();

        advance();          // skip @sof, so prev() is always valid
        match_newlines();   // skip possible leading '\n'

        if(mode()==EVAL_MODE) {
            EXPR_TUPLE(false);
            consume(TK("@eof"));
            ctx()->emit(OP_RETURN_VALUE, BC_NOARG, BC_KEEPLINE);
            pop_context();
            return code;
        }else if(mode()==JSON_MODE){
            EXPR();
            Expr_ e = ctx()->s_expr.popx();
            if(!e->is_json_object()) SyntaxError("expect a JSON object, literal or array");
            consume(TK("@eof"));
            e->emit(ctx());
            ctx()->emit(OP_RETURN_VALUE, BC_NOARG, BC_KEEPLINE);
            pop_context();
            return code;
        }

        while (!match(TK("@eof"))) {
            if (match(TK("class"))) {
                compile_class();
            } else {
                compile_stmt();
            }
            match_newlines();
        }
        pop_context();
        return code;
    }
};

} // namespace pkpy