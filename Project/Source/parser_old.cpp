
#include "parser.h"

// TODO: need to refactor and rewrite some of the stuff, return types don't work yet.

// Returns value of operator precedence (1 is the lowest),
// and returns -1 if the supplied token is not an operator
static int GetOperatorPrecedence(Token* token);

static Ast_ExprNode* ParsePrimaryExpression(Parser* p);
static Ast_ExprNode* ParseExpression(Parser* p,
                                     int prec = INT_MIN);
static Ast_ExprNode* ParseExpression(Parser* p,
                                     Ast_ExprNode* left,
                                     int prec = INT_MIN);
static Array<Ast_ExprNode*> ParseExpressionArray(Parser* p, bool* err);

struct TryParse_Ret
{
    Ast_ExprNode* expr;
    bool error;
};
static TryParse_Ret TryParseExpression(Parser* p,
                                       TokenType expected);
static Ast_Function* ParseDefinition(Parser* p);
static Ast_Prototype* ParseExtern(Parser* p);

static Ast_Stmt* ParseStatement(Parser* p);
//static Ast_If* ParseIfStatement(Parser* p);
//static Ast_For* ParseForStatement(Parser* p);
// TODO: this will probably replace the previous
// "ParseExpressionOrDeclaration"
static Ast_Stmt* ParseDeclOrExpr(Parser* p,
                                 bool forceInitialization);
#if 0
struct TryParseDecl_Ret
{
    Ast_ExprOrDecl* node;
    bool error;
};
static TryParseDecl_Ret TryParseExpressionOrDeclaration(Parser* p,
                                                        bool forceInitialization,
                                                        TokenType expected);
#endif

//static Ast_While* ParseWhileStatement(Parser* p);
static Ast_ReturnStmt* ParseReturnStatement(Parser* p);
//static Ast_DeclStmt* ParseDeclarationStatement(Parser* p,
//bool forceInitialization);
static Ast_BlockStmt* ParseBlockStatement(Parser* p);
//static Type ParseType(Parser* p, bool* err);

static void SyntaxError(Token token, Parser* tokenizer, char* message);
static void SyntaxError(Parser* parser, char* message);

static void SyntaxError(Token token, Parser* parser, char* message)
{
    CompilationError(token, parser->tokenizer, "Syntax error", message);
}

static void SyntaxError(Parser* parser, char* message)
{
    CompilationError(parser->tokenizer, "Syntax error", message);
}

inline int GetOperatorPrecedence(Token* token)
{
    switch(token->type)
    {
        default: return -1;
        
        case Tok_LE:
        case Tok_GE:
        case Tok_EQ:
        case Tok_NEQ:
        case Tok_LShiftEquals:
        case Tok_LShift:
        case '<':
        case '>': return 10;
        case '+':
        case '-': return 20;
        case '/':
        case '*': return 40;
        case '=': return 5;
    }
}

enum OperatorPos
{
    Prefix,
    Binary,
    Postfix
};
// We could also include associativity in this
inline int GetOperatorPrecedence2(Token* token, OperatorPos operatorPos)
{
    if(operatorPos == Prefix)
    {
        switch(token->type)
        {
            default: return -1;
            
            case Tok_Increment:
            case Tok_Decrement:
            case '+':
            case '-':
            case '!':
            case '~':
            case '*':
            case '&':
            return 2;
        }
    }
    else if(operatorPos == Binary)
    {
        switch(token->type)
        {
            default: return -1;
            
            case '*':
            case '/':
            case '%':
            return 3;
            
            case '+':
            case '-':
            return 4;
            
            case Tok_LShift:
            case Tok_RShift:
            return 5;
            
            case '<':
            case Tok_LE:
            case '>':
            case Tok_GE:
            return 6;
            
            case Tok_EQ:
            case Tok_NEQ:
            return 7;
            
            case '&':
            return 8;
            
            case '^':
            return 9;
            
            case '|':
            return 10;
            
            case Tok_And:
            return 11;
            
            case Tok_Or:
            return 12;
            
            case '=':
            case Tok_PlusEquals:
            case Tok_MinusEquals:
            case Tok_MulEquals:
            case Tok_DivEquals:
            case Tok_ModEquals:
            case Tok_LShiftEquals:
            case Tok_RShiftEquals:
            case Tok_AndEquals:
            case Tok_XorEquals:
            case Tok_OrEquals:
            return 14;
        }
    }
    else if(operatorPos == Postfix)
    {
        switch(token->type)
        {
            default: return -1;
            
            case Tok_Increment:
            case Tok_Decrement:
            return 1;
        }
    }
    else
        return -1;
}

inline bool IsUnaryOperator(Token* token)
{
    switch(token->type)
    {
        default:  return false;
        case '*':
        case '&':
        case '-':
        case '+': return true;
    }
}

inline bool IsOperatorLeftToRightAssociative(Token* token)
{
    if(token->type == '=')
        return false;
    return true;
}

inline Ast_ExprType TokenToExprType(Token* token)
{
    switch(token->type)
    {
        default:         return IdentNode;
        case Tok_Ident: return IdentNode;
        case Tok_Num:   return NumNode;
    }
}

inline bool IsTokTypeIdent(TokenType tokType)
{
    return tokType >= Tok_IdentBegin && tokType <= Tok_IdentEnd;
};

// TODO: This function should be refactored
static Ast_ExprNode* ParsePrimaryExpression(Parser* p)
{
    ProfileFunc();
    
    Ast_ExprNode* node = 0;
    Token* token = PeekNextToken(p->tokenizer);
    
    // It's a function call
    if(IsTokTypeIdent(token->type) &&
       PeekToken(p->tokenizer, 2)->type == '(')
    {
        auto callNode = Arena_AllocAndInit(p->arena, Ast_CallExprNode);
        callNode->token       = *token;
        callNode->args.ptr    = 0;
        callNode->args.length = 0;
        
        EatToken(p->tokenizer);  // Eat identifier
        EatToken(p->tokenizer);  // Eat '('
        
        TempArenaMemory savepoint = Arena_TempBegin(p->tempArena);
        defer(Arena_TempEnd(savepoint));
        
        Array<Ast_ExprNode*> argsArray = { 0, 0 };
        Token* token = PeekNextToken(p->tokenizer);
        if(token->type != ')')
        {
            int curArg = 0;
            while(true)
            {
                Ast_ExprNode* tmp = ParseExpression(p);
                if(!tmp)
                    return 0;
                
                Array_AddElement(&argsArray, p->tempArena, tmp);
                
                token = PeekNextToken(p->tokenizer);
                if(token->type == ')')
                    break;
                
                if(token->type != ',')
                {
                    SyntaxError(p, "Expected ','");
                    return 0;
                }
                
                EatToken(p->tokenizer);  // Eat ','
            }
            
            callNode->args = Array_CopyToArena(argsArray, p->arena);
        }
        
        node = callNode;
        EatToken(p->tokenizer);  // Eat ')'
    }
    // It's a subscript expression
    else if(IsTokTypeIdent(token->type) &&
            PeekToken(p->tokenizer, 2)->type == '[')
    {
        Token ident = *token;
        EatToken(p->tokenizer);  // Eat ident
        EatToken(p->tokenizer);  // Eat '['
        
        auto subscript = Arena_AllocAndInit(p->arena, Ast_SubscriptExprNode);
        subscript->token = ident;
        subscript->indexExpr = ParseExpression(p);
        if(!subscript->indexExpr)
            return 0;
        
        if(PeekNextToken(p->tokenizer)->type != ']')
        {
            SyntaxError(p, "Expected ']'");
            return 0;
        }
        
        EatToken(p->tokenizer);  // Eat ']'
        node = subscript;
    }
    // It's an identifier/literal
    else if(IsTokTypeIdent(token->type) || token->type == Tok_Num)
    {
        node = Arena_AllocAndInit(p->arena, Ast_ExprNode);
        node->token = *token;
        node->type = TokenToExprType(token);
        
        EatToken(p->tokenizer);  // Eat ident/num
    }
    // Parenthesis expression
    else if(token->type == '(')
    {
        EatToken(p->tokenizer);  // Eat '('
        node = ParseExpression(p);
        if(PeekNextToken(p->tokenizer)->type == ')')
            EatToken(p->tokenizer);  // Eat ')'
        else
        {
            SyntaxError(p, "Expected ')' at the end of expression");
            return 0;
        }
    }
    // Unary expression
    // You could accomplish the same thing by using
    // a different precedence for the unary operator - or something.
    // I wonder if that would be better
    else if(IsUnaryOperator(token))
    {
        Token unary = *token;
        EatToken(p->tokenizer);  // Eat unary
        
        auto unaryNode = Arena_AllocAndInit(p->arena, Ast_UnaryExprNode);
        unaryNode->token = unary;
        unaryNode->expr = ParsePrimaryExpression(p);
        node = unaryNode;
    }
    // Array expression
    else if(token->type == '{')
    {
        Token bracket = *token;
        EatToken(p->tokenizer);  // Eat '}'
        
        auto arrayNode = Arena_AllocAndInit(p->arena, Ast_ArrayInitNode);
        arrayNode->token = bracket;
        bool err = false;
        arrayNode->expressions = ParseExpressionArray(p, &err);
        if(err)
            return 0;
        
        node = arrayNode;
        EatToken(p->tokenizer);
    }
    else if(token->type != Tok_Error)
    {
        //SyntaxError(*token, p->tokenizer, "Expected expression");
        //return 0;
    }
    return node;
}

static Ast_ExprNode* ParseExpression(Parser* p,
                                     int prec)
{
    ProfileFunc();
    
    Ast_ExprNode* left = ParsePrimaryExpression(p);
    if(!left)
        return 0;
    
    while(true)
    {
        Token* token = PeekNextToken(p->tokenizer);
        int tokenPrec = GetOperatorPrecedence(token);
        // Stop the recursion
        if(tokenPrec == prec)
        {
            if(IsOperatorLeftToRightAssociative(token))
                return left;
        }
        else if(tokenPrec < prec || tokenPrec == -1)
            return left;
        
        // This is a binary operation
        
        auto node = Arena_AllocAndInit(p->arena, Ast_BinExprNode);
        node->token = *token;
        node->left  = left;
        EatToken(p->tokenizer);  // Eat operator
        node->right = ParseExpression(p, tokenPrec);
        
        if(!node->right)
            return 0;
        
        left = node;
    }
}

static Ast_ExprNode* ParseExpression(Parser* p,
                                     Ast_ExprNode* left,
                                     int prec)
{
    ProfileFunc();
    
    while(true)
    {
        Token* token = PeekNextToken(p->tokenizer);
        int tokenPrec = GetOperatorPrecedence(token);
        // Stop the recursion
        if(tokenPrec == prec)
        {
            if(IsOperatorLeftToRightAssociative(token))
                return left;
        }
        else if(tokenPrec < prec || tokenPrec == -1)
            return left;
        
        // This is a binary operation
        
        auto node = Arena_AllocAndInit(p->arena, Ast_BinExprNode);
        node->token = *token;
        node->left  = left;
        EatToken(p->tokenizer);  // Eat operator
        node->right = ParseExpression(p, tokenPrec);
        
        if(!node->right)
            return 0;
        
        left = node;
    }
}

// TODO: I think this could be refactored
static TryParse_Ret TryParseExpression(Parser* p, TokenType expected)
{
    ProfileFunc();
    
    Ast_ExprNode* node = 0;
    
    if(PeekNextToken(p->tokenizer)->type != expected)
    {
        node = ParseExpression(p, false);
        if(PeekNextToken(p->tokenizer)->type != expected)
        {
            // TODO: Print expected token here
            SyntaxError(p, "Unexpected token");
            TryParse_Ret ret { 0, true };
            return ret;
        }
    }
    
    EatToken(p->tokenizer);   // Eat 'expected'
    TryParse_Ret ret { node, false };
    return ret;
}

static Ast_Prototype* ParsePrototype(Parser* p)
{
    ProfileFunc();
    
    Token token = *PeekNextToken(p->tokenizer);
    
    if(token.type != Tok_Ident)
    {
        SyntaxError(p, "Expected function name in prototype");
        return 0;
    }
    
    String ident = token.ident;
    EatToken(p->tokenizer);   // Eat identifier
    
    if(PeekNextToken(p->tokenizer)->type != '(')
    {
        SyntaxError(p, "Expected '(' in prototype");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat (
    
    auto node = Arena_AllocAndInit(p->arena, Ast_Prototype);
    node->token       = token;
    node->args.ptr    = 0;
    node->args.length = 0;
    
    Token currentToken = *PeekNextToken(p->tokenizer);
    bool continueLoop = IsTokTypeIdent(currentToken.type);
    while(continueLoop)
    {
        if(currentToken.type != Tok_Ident)
        {
            SyntaxError(p, "Expected identifier");
            return 0;
        }
        
        EatToken(p->tokenizer);  // Eat ident
        
        bool isPtr = false;
        if(PeekNextToken(p->tokenizer)->type == '*')
        {
            isPtr = true;
            EatToken(p->tokenizer);  // Eat '*'
        }
        
        Ast_ProtoArg arg = { currentToken, isPtr };
        Array_AddElement(&node->args, p->arena, arg);
        
        if(PeekNextToken(p->tokenizer)->type == ',')
            EatToken(p->tokenizer);  // Eat ','
        else
            continueLoop = false;
        
        currentToken = *PeekNextToken(p->tokenizer);
    }
    
    if(PeekNextToken(p->tokenizer)->type != ')')
    {
        SyntaxError(p, "Expected ')' at end of prototype");
        EatToken(p->tokenizer);
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat ')'
    
    // If there are return values
    if(PeekNextToken(p->tokenizer)->type == Tok_Arrow)
    {
        EatToken(p->tokenizer);  // Eat "->"
        
        Token token = *PeekNextToken(p->tokenizer);
        if(token.type != Tok_Ident)
        {
            SyntaxError(p, "Expecting return type(s) after '->'");
            return 0;
        }
        
        auto savepoint = Arena_TempBegin(p->tempArena);
        defer(Arena_TempEnd(savepoint));
        
#if 0
        // NOTE(Leo): Ignoring return names, for now.
        Array<Type> types = { 0, 0 };
        while(true)
        {
            bool err = false;
            Type type = ParseType(p, &err);
            if(err)
                return 0;
            
            Array_AddElement(&types, p->tempArena, type);
            
            if(IsTokTypeIdent(PeekNextToken(p->tokenizer)->type))
                EatToken(p->tokenizer);  // Eat ident
            
            Token token = *PeekNextToken(p->tokenizer);
            if(token.type != ',')
                break;
            
            EatToken(p->tokenizer);  // Eat ','
        }
        
        types = Array_CopyToArena(types, p->arena);
        
        node->retTypes = types;
#endif
    }
    
    return node;
}

static Ast_Function* ParseDefinition(Parser* p)
{
    ProfileFunc();
    
    EatToken(p->tokenizer);   // Eat def
    
    auto node = Arena_AllocAndInit(p->arena, Ast_Function);
    node->prototype = ParsePrototype(p);
    if(!node->prototype)
        return 0;
    node->body = ParseBlockStatement(p);
    if(!node->body)
        return 0;
    
    return node;
}

static Ast_Prototype* ParseExtern(Parser* p)
{
    ProfileFunc();
    
    EatToken(p->tokenizer);   // Eat extern
    
    Ast_Prototype* proto = ParsePrototype(p);
    
    Token next = *PeekNextToken(p->tokenizer);
    if(next.type != ';')
    {
        SyntaxError(p, "Expected ';'");
        return 0;
    }
    
    return proto;
}

static Ast_Stmt* ParseStatement(Parser* p)
{
    ProfileFunc();
    
    Ast_Stmt* result = 0;
    bool expectingSemicolon = true;
    switch (PeekNextToken(p->tokenizer)->type)
    {
        // Empty statement
        case ';':
        {
            EatToken(p->tokenizer);   // Eat ';'
            expectingSemicolon = false;
            result = Arena_AllocAndInit(p->arena, Ast_Stmt);
            result->type = EmptyStmt;
        } break;
        
        case Tok_Return:
        {
            result = ParseReturnStatement(p);
        }
        break;
        
        case '{':
        {
            result = ParseBlockStatement(p);
            expectingSemicolon = false;
        }
        break;
        
#if 0
        case TokenIf:
        {
            result = ParseIfStatement(p);
            expectingSemicolon = false;
        }
        break;
        
        case TokenFor:
        {
            result = ParseForStatement(p);
            expectingSemicolon = false;
        }
        break;
        
        case TokenWhile:
        {
            result = ParseWhileStatement(p);
            expectingSemicolon = false;
        }
        break;
#endif
        
        default:
        {
            // If it's none of the keywords,
            // then it could either be a declaration or
            // an expression.
            result = ParseDeclOrExpr(p, false);
        }
        break;
    }
    
    if(result && expectingSemicolon)
    {
        if(PeekNextToken(p->tokenizer)->type != ';')
        {
            SyntaxError(p, "Expected ';' at the end of statement");
            return 0;
        }
        
        EatToken(p->tokenizer);
    }
    
    return result;
}

// If, for and while statements are ignored for now.
#if 0
static Ast_If* ParseIfStatement(Parser* p)
{
    ProfileFunc();
    
    if(PeekNextToken(p->tokenizer)->type != TokenIf)
    {
        SyntaxError(p, "Expected if statement");
        return 0;
    }
    
    if(PeekToken(p->tokenizer, 2)->type != '(')
    {
        SyntaxError(p, "Expected '('");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat token 'if'
    EatToken(p->tokenizer);   // Eat token '('
    
    auto ifStmt  = Arena_AllocAndInit(p->arena, Ast_If);
    ifStmt->cond = ParseExpressionOrDeclaration(p, true);
    if(!ifStmt->cond)
        return 0;
    
    if(PeekNextToken(p->tokenizer)->type != ')')
    {
        SyntaxError(p, "Expected ')' at the end of condition");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat token ')'
    
    ifStmt->trueStmt = ParseStatement(p);
    if(!ifStmt->trueStmt)
        return 0;
    if(PeekNextToken(p->tokenizer)->type == TokenElse)
    {
        EatToken(p->tokenizer);   // Eat 'else'
        ifStmt->falseStmt = ParseStatement(p);
        if(!ifStmt->falseStmt)
            return 0;
    }
    
    return ifStmt;
}

static Ast_For* ParseForStatement(Parser* p)
{
    ProfileFunc();
    
    if(PeekNextToken(p->tokenizer)->type != TokenFor)
    {
        SyntaxError(p, "Expected for statement");
        return 0;
    }
    
    if(PeekToken(p->tokenizer, 2)->type != '(')
    {
        SyntaxError(p, "Expected '('");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat 'for'
    EatToken(p->tokenizer);   // Eat '('
    
    auto forStmt  = Arena_AllocAndInit(p->arena, Ast_For);
    
    // Try to parse the decl/expr only if it's not empty
    TryParseDecl_Ret result;
    result = TryParseExpressionOrDeclaration(p, false, (TokenType)';');
    forStmt->init = result.node;
    if(result.error)
        return 0;
    result = TryParseExpressionOrDeclaration(p, true, (TokenType)';');
    forStmt->cond = result.node;
    if(result.error)
        return 0;
    TryParse_Ret exprResult;
    exprResult = TryParseExpression(p, (TokenType)')');
    forStmt->iteration = exprResult.expr;
    if(exprResult.error)
        return 0;
    forStmt->execStmt = ParseStatement(p);
    if(!forStmt->execStmt)
        return 0;
    
    return forStmt;
}

static Ast_While* ParseWhileStatement(Parser* p)
{
    ProfileFunc();
    
    if(PeekNextToken(p->tokenizer)->type != TokenWhile)
    {
        SyntaxError(p, "Expected while statement");
        return 0;
    }
    
    if(PeekToken(p->tokenizer, 2)->type != '(')
    {
        SyntaxError(p, "Expected '('");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat 'while'
    EatToken(p->tokenizer);   // Eat '('
    
    auto whileStmt = Arena_AllocAndInit(p->arena, Ast_While);
    whileStmt->cond = ParseExpressionOrDeclaration(p, true);
    if(!whileStmt->cond)
        return 0;
    
    if(PeekNextToken(p->tokenizer)->type != ')')
    {
        SyntaxError(p, "Expected ')' at the end of condition");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat ')'
    
    whileStmt->execStmt = ParseStatement(p);
    if(!whileStmt->execStmt)
        return 0;
    
    return whileStmt;
}
#endif

// Expressions separated with ','
static Array<Ast_ExprNode*> ParseExpressionArray(Parser* p, bool* err)
{
    Array<Ast_ExprNode*> result = { 0, 0 };
    
    while(true)
    {
        auto expr = ParseExpression(p);
        if(!expr)
        {
            *err = true;
            return result;
        }
        
        Array_AddElement(&result, p->tempArena, expr);
        
        if(PeekNextToken(p->tokenizer)->type != ',')
            break;
        
        EatToken(p->tokenizer);  // Eat ','
    }
    
    result = Array_CopyToArena(result, p->arena);
    return result;
};

#if 0
static Type ParseType(Parser* p, bool* err)
{
    Type result;
    
    Token t = *PeekNextToken(p->tokenizer);
    uint8 numPointers = 0;
    while(t.type == '^')
    {
        EatToken(p->tokenizer);
        t = *PeekNextToken(p->tokenizer);
        
        //++result.numPointers;
    }
    
    t = *PeekNextToken(p->tokenizer);
    if(t.type != TokenIdent)
    {
        SyntaxError(p, "Expecting identifier here");
        *err = true;
        return result;
    }
    
    EatToken(p->tokenizer);  // Eat ident
    
    result.baseType = t;
    if(PeekNextToken(p->tokenizer)->type == '(')
    {
        EatToken(p->tokenizer);  // Eat '('
        result.params = ParseExpressionArray(p, err);
        if(*err)
            return result;
        
        if(PeekNextToken(p->tokenizer)->type != ')')
        {
            SyntaxError(p, "Expecting ')'");
            *err = true;
            return result;
        }
        
        EatToken(p->tokenizer);  // Eat ')'
    }
    
    return result;
}
#endif

static Ast_Stmt* ParseDeclOrExpr(Parser* p,
                                 bool forceInitialization)
{
    Ast_Stmt* result = 0;
    
    if(PeekNextToken(p->tokenizer)->type == ';')
    {
        EatToken(p->tokenizer);  // Eat ';'
        return result;
    }
    
    auto savepoint = Arena_TempBegin(p->tempArena);
    defer(Arena_TempEnd(savepoint));
    
    Token t = *PeekNextToken(p->tokenizer);
    
    Type* curType = 0;
    Type* firstType = 0;
    
    // If determined to be a declaration
    bool isDecl = false;
    
    // Construct linked list
    while(t.type == '^' || t.type == '[')
    {
        auto tmpType = Arena_AllocAndInit(p->arena, Type);
        tmpType->typeId = (TypeId)t.type;
        
        if(!firstType)
            firstType = tmpType;
        
        if(t.type == '^')
        {
            if(curType)
                curType->ptrType.baseType = tmpType;
            curType = tmpType;
        }
        else if(t.type == '[')
        {
            tmpType->typeId = (TypeId)Tok_Subscript;
            
            EatToken(p->tokenizer);
            
            auto expr = ParseExpression(p);
            if(!expr)
                return 0;
            
            if(PeekNextToken(p->tokenizer)->type != ']')
            {
                SyntaxError(p, "Expecting ']'");
                return 0;
            }
            
            tmpType->arrayType.arrSizeExpr = expr;
            tmpType->arrayType.arrSizeValue = 0;
            
            curType = tmpType;
        }
        else if(t.type == Tok_Proc)
        {
            
        }
        else
            break;
        
        EatToken(p->tokenizer);
        t = *PeekNextToken(p->tokenizer);
    }
    
    Array<Ast_ExprNode*> exprArr = { 0, 0 };
    Token ident;  // Either for function call or for base type
    Token openParen;
    // There is a function call or a polymorphic type
    bool callOrPoly = false;
    
    // NOTE: there has to be a single element in here,
    // if it was a polymorphic struct 
    if(IsTokTypeIdent(PeekToken(p->tokenizer, 1)->type) &&
       PeekToken(p->tokenizer, 2)->type == '(')
    {
        callOrPoly = true;
        ident = *PeekNextToken(p->tokenizer);
        openParen = *PeekToken(p->tokenizer, 2);
        
        EatToken(p->tokenizer);
        EatToken(p->tokenizer);
        
        if(PeekNextToken(p->tokenizer)->type != ')')
        {
            bool err = false;
            exprArr = ParseExpressionArray(p, &err);
            if(err)
                return 0;
        }
        
        if(PeekNextToken(p->tokenizer)->type != ')')
        {
            SyntaxError(p, "Expecting ')'");
            return 0;
        }
        
        EatToken(p->tokenizer);  // Eat ')'
        
        if(IsTokTypeIdent(PeekNextToken(p->tokenizer)->type))
            isDecl = true;
    }
    else if(IsTokTypeIdent(PeekToken(p->tokenizer, 1)->type) &&
            IsTokTypeIdent(PeekToken(p->tokenizer, 2)->type))
    {
        ident = *PeekNextToken(p->tokenizer);
        
        isDecl = true;
        EatToken(p->tokenizer);
    }
    
    if(isDecl)
    {
        if(callOrPoly && exprArr.length == 0)
        {
            SyntaxError(openParen, p, "Assumed declaration [TODO: why?]; Polymorphic type is required to have at least 1 parameter.");
            return 0;
        }
        
        auto decl = Arena_AllocAndInit(p->arena, Ast_Declaration);
        
        // Append the base type
        auto tmpType = Arena_AllocAndInit(p->arena, Type);
        tmpType->typeId = (TypeId)ident.type;
        
        // @typesafety
        if(firstType)
            curType->ptrType.baseType = tmpType;
        else
            firstType = tmpType;
        
        decl->typeInfo = firstType;
        decl->nameIdent = *PeekNextToken(p->tokenizer);
        if(!IsTokTypeIdent(decl->nameIdent.type))
        {
            SyntaxError(p, "Unexpected token");
            return 0;
        }
        
        EatToken(p->tokenizer);
        
        // Initialization
        Token* t = PeekNextToken(p->tokenizer);
        if(t->type == '=')
        {
            decl->equal = *t;
            EatToken(p->tokenizer);
            
            decl->initExpr = ParseExpression(p);
            if(!decl->initExpr)
                return 0;
        }
        
        result = decl;
    }
    else
    {
        if(callOrPoly)
        {
            auto call = Arena_AllocAndInit(p->arena, Ast_CallExprNode);
            call->token = ident;
            call->args  = exprArr;
            
            auto exprStmt = Arena_AllocAndInit(p->arena, Ast_ExprStmt);
            exprStmt->expr = ParseExpression(p, call);
            if(!exprStmt->expr)
                return 0;
            
            result = exprStmt;
        }
        else
        {
            auto exprStmt = Arena_AllocAndInit(p->arena, Ast_ExprStmt);
            exprStmt->expr = ParseExpression(p);
            
            result = exprStmt;
        }
    }
    
    Assert(result->type == DeclStmt || result->type == ExprStmt);
    return result;
}

/*
static TryParseDecl_Ret TryParseExpressionOrDeclaration(Parser* p,
                                                        bool forceInitialization,
                                                        TokenType expected)
{
    ProfileFunc();
    
    Ast_ExprOrDecl* exprOrDecl = 0;
    if(PeekNextToken(p->tokenizer)->type != expected)
    {
        exprOrDecl = ParseExpressionOrDeclaration(p, forceInitialization);
        if(PeekNextToken(p->tokenizer)->type != expected)
        {
            // TODO(Leo): print expected token
            SyntaxError(p, "Unexpected token");
            TryParseDecl_Ret ret = { 0, true };
        }
    }
    
    EatToken(p->tokenizer);   // Eat 'expected'
    return { exprOrDecl, false };
}
*/

/*
static Ast_DeclStmt* ParseDeclarationStatement(Parser* p,
                                               bool forceInitialization)
{
    ProfileFunc();
    
    EatToken(p->tokenizer);  // Eat 'var'
    
    auto block = Arena_AllocAndInit(p->arena, Ast_DeclStmt);
    block->expr = 0;
    
    if(PeekNextToken(p->tokenizer)->type == '*')
    {
        block->isPointer = true;
        EatToken(p->tokenizer);  // Eat '*'
    }
    else
        block->isPointer = false;
    
    Token identifier = *PeekNextToken(p->tokenizer);
    if (identifier.type != TokenIdent)
    {
        SyntaxError(p, "Expected identifier");
        return 0;
    }
    
    block->id = identifier;
    
    EatToken(p->tokenizer);
    Token equal = *PeekNextToken(p->tokenizer);
    
    if (equal.type == '=')
    {
        block->equal = equal;
        EatToken(p->tokenizer);
        block->expr = ParseExpression(p);
        if (!block->expr)
            return 0;
    }
    else if(forceInitialization)
    {
        SyntaxError(p, "Expected '=', variable has to be initialized");
        return 0;
    }
    
    return block;
}
*/

static Ast_ReturnStmt* ParseReturnStatement(Parser* p) 
{
    ProfileFunc();
    
    if (PeekNextToken(p->tokenizer)->type != Tok_Return)
    {
        SyntaxError(p, "Expected 'return'");
        return 0;
    }
    
    EatToken(p->tokenizer);
    
    auto stmt = Arena_AllocAndInit(p->arena, Ast_ReturnStmt);
    
    if (PeekNextToken(p->tokenizer)->type != ';')
        stmt->returnValue = ParseExpression(p);
    else
    {
        SyntaxError(p, "Expected return value");
        return 0;
    }
    
    return stmt;
}

static Ast_BlockStmt* ParseBlockStatement(Parser* p)
{
    ProfileFunc();
    
    if(PeekNextToken(p->tokenizer)->type != '{')
    {
        SyntaxError(p, "Expected '{' for the beginning of the block");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat '{'
    
    auto block = Arena_AllocAndInit(p->arena, Ast_BlockStmt);
    block->first = 0;
    
    Ast_BlockNode* prevBlockNode = 0;
    while(PeekNextToken(p->tokenizer)->type != '}')
    {
        auto blockNode = Arena_AllocAndInit(p->arena, Ast_BlockNode);
        blockNode->stmt = ParseStatement(p);
        if(!blockNode->stmt)
            return 0;
        
        if(block->first)
            prevBlockNode->next = blockNode;
        else
            block->first = blockNode;
        
        prevBlockNode = blockNode;
    }
    
    if(PeekNextToken(p->tokenizer)->type != '}')
    {
        SyntaxError(p, "Expected '}' for block ending");
        return 0;
    }
    
    EatToken(p->tokenizer);   // Eat '}'
    
    return block;
}

bool ParseRootNode(Parser *parser, Tokenizer *tokenizer)
{
    ProfileFunc();
    
    auto savepoint = Arena_TempBegin(parser->tempArena);
    defer(Arena_TempEnd(savepoint));
    
    Token* token = PeekNextToken(tokenizer);
    bool endOfFile = false;
    // Top level statements
    while(true)
    {
        bool skipToNext = false;
        bool parseSuccess = false;
        Ast_TopLevelStmt *topLevel = nullptr;
        switch(token->type)
        {
            // Ignore top-level semicolons
            case ';':
            {
                EatToken(tokenizer);
                token = PeekNextToken(tokenizer);
                skipToNext = true;
            }
            break;
            case Tok_Proc:
            {
                topLevel = ParseDefinition(parser);
                if(topLevel)
                    parseSuccess = true;
            }
            break;
            case Tok_Extern:
            {
                topLevel = ParseExtern(parser);
                if(topLevel)
                    parseSuccess = true;
            }
            break;
            case Tok_Error: { parseSuccess = false; } break;
            case Tok_EOF: { endOfFile = true; } break;
            default:
            {
                SyntaxError(*token, parser, "Unexpected token");
                parseSuccess = false;
            } break;
        }
        
        if(skipToNext)
            continue;
        if(endOfFile)
            break;
        if(!parseSuccess || !topLevel)
            return false;
        
        Array_AddElement(&parser->root.topStmts, parser->tempArena, topLevel);
        token = PeekNextToken(tokenizer);    
    }
    
    parser->root.topStmts = Array_CopyToArena(parser->root.topStmts, parser->arena);
    return true;
}