#include <cassert>
#include <cinttypes>
#include <cstring>
#include <iostream>
#include "parser.h"

/// Object member operator
const OpInfo OP_MEMBER = { ".", "", 2, 16, 'l', false, false };

/// Array indexing
const OpInfo OP_INDEX = { "[", "]", 2, 16, 'l', false, false };

/// Object extension
const OpInfo OP_OBJ_EXT = { "::", "", 2, 16, 'l', false, false };

/// Function call, variable arity
const OpInfo OP_CALL = { "(", ")", -1, 15, 'l', false, false };

/// Method call
const OpInfo OP_M_CALL = { ":", "", 2, 14, 'l', false, false };

/// Prefix unary operators
const OpInfo OP_NEG = { "-", "", 1, 13, 'r', false, false };
const OpInfo OP_NOT = { "!", "", 1, 13, 'r', false, false };
const OpInfo OP_BIT_NOT = { "~", "", 1, 13, 'r', false, false };
const OpInfo OP_TYPEOF = { "typeof", "", 1, 13, 'r', false, false };

/// Binary arithmetic operators
const OpInfo OP_MUL = { "*", "", 2, 12, 'l', false, true };
const OpInfo OP_DIV = { "/", "", 2, 12, 'l', true, true };
const OpInfo OP_MOD = { "%", "", 2, 12, 'l', true, true };
const OpInfo OP_ADD = { "+", "", 2, 11, 'l', false, true };
const OpInfo OP_SUB = { "-", "", 2, 11, 'l', true, true };

/// Relational operators
const OpInfo OP_LT = { "<", "", 2, 9, 'l', false, false };
const OpInfo OP_LE = { "<=", "", 2, 9, 'l', false, false };
const OpInfo OP_GT = { ">", "", 2, 9, 'l', false, false };
const OpInfo OP_GE = { ">=", "", 2, 9, 'l', false, false };
const OpInfo OP_IN = { "in", "", 2, 9, 'l', false, false };
const OpInfo OP_INSTOF = { "instanceof", "", 2, 9, 'l', false, false };

/// Equality comparison
const OpInfo OP_EQ = { "==", "", 2, 8, 'l', false, false };
const OpInfo OP_NE = { "!=", "", 2, 8, 'l', false, false };

/// Bitwise operators
const OpInfo OP_BIT_AND = { "&", "", 2, 7, 'l', true, true };
const OpInfo OP_BIT_XOR = { "^", "", 2, 6, 'l', true, true };
const OpInfo OP_BIT_OR = { "|", "", 2, 5, 'l', true, true };
const OpInfo OP_BIT_SHL  = { "<<", "", 2, 10, 'l', false, true };  // shift left
const OpInfo OP_BIT_SHR  = { ">>", "", 2, 10, 'l', false, true };  // sign-extending shift right
const OpInfo OP_BIT_USHR = { ">>>", "", 2, 10, 'l', false, true }; // unsigned shift right

/// Logical operators
const OpInfo OP_AND = { "&&", "", 2, 4, 'l', true };
const OpInfo OP_OR = { "||", "", 2, 3, 'l', true };

// Assignment
const OpInfo OP_ASSIGN = { "=", "", 2, 1, 'r', false };

/// Read an entire file at once
std::string readFile(std::string fileName)
{
    FILE* file = fopen(fileName.c_str(), "rb");

    if (!file)
    {
        fprintf(stderr, "failed to open file \"%s\"\n", fileName.c_str());
        exit(-1);
    }

    // Get the file size in bytes
    fseek(file, 0, SEEK_END);
    size_t len = ftell(file);
    fseek(file, 0, SEEK_SET);

    //printf("%ld bytes\n", len);

    char* buf = (char*)malloc(len+1);

    // Read into the allocated buffer
    size_t read = fread(buf, 1, len, file);

    if (read != len)
    {
        printf("failed to read file");
        assert (false);
    }

    // Add a null terminator to the string
    buf[len] = '\0';

    // Close the input file
    fclose(file);

    return std::string(buf);
}

Input::Input(std::string str, std::string srcName)
{
    this->srcName = srcName;
    this->inStr = str;
    this->strIdx = 0;
    this->lineNo = 1;
    this->colNo = 1;
}

Input::~Input()
{
}

/// Peek at a character from the input
char Input::peekCh()
{
    if (strIdx >= inStr.length())
        return '\0';

    return inStr[strIdx];
}

/// Read a character from the input
char Input::readCh()
{
    char ch = peekCh();

    // Strictly reject invalid input characters
    if ((ch < 0x20 || ch > 0x7E) &&
        (ch != '\n' && ch != '\t' && ch != '\r'))
    {
        char hexStr[64];
        sprintf(hexStr, "0x%02X", (int)ch);
        throw ParseError(
            *this,
            "invalid character in input, " + std::string(hexStr)
        );
    }

    this->strIdx++;

    if (ch == '\n')
    {
        lineNo++;
        colNo = 1;
    }
    else
    {
        colNo++;
    }

    return ch;
}

/// Test if the end of file has been reached
bool Input::eof()
{
    return peekCh() == '\0';
}

/// Peek to check if a string is next in the input
bool Input::next(const std::string& str)
{
    size_t idx = 0;

    for (; idx < str.length(); idx++)
    {
        if (this->strIdx + idx >= this->inStr.length())
            return false;

        if (str[idx] != this->inStr[this->strIdx + idx])
            return false;
    }

    return true;
}

/// Try and match a given string in the input
/// The string is consumed if matched
bool Input::match(const std::string& str)
{
    assert (str.length() > 0);

    if (next(str))
    {
        for (size_t i = 0; i < str.length(); ++i)
            readCh();

        return true;
    }

    return false;
}

/// Fail if the input doesn't match a given string
void Input::expect(const std::string str)
{
    if (!match(str))
    {
        throw ParseError(*this, "expected to find '" + str + "'");
    }
}

/// Consume whitespace and comments
void Input::eatWS()
{
    //std::cout << "entering eatWS" << std::endl;

    // Until the end of the whitespace
    for (;;)
    {
        // If we are at the end of the input, stop
        if (eof())
        {
            return;
        }

        // Consume whitespace characters
        if (isspace(peekCh()))
        {
            readCh();
            continue;
        }

        // If this is a single-line comment
        if (match("//"))
        {
            // Read until and end of line is reached
            for (;;)
            {
                if (eof())
                    return;

                if (readCh() == '\n')
                    break;
            }

            continue;
        }

        // If this is a multi-line comment
        if (match("/*"))
        {
            // Read until the end of the comment
            for (;;)
            {
                if (eof())
                {
                    throw ParseError(
                        *this,
                        "end of input in multiline comment"
                    );
                }

                if (readCh() == '*' && match("/"))
                {
                    break;
                }
            }

            continue;
        }

        // This isn't whitespace, stop
        break;
    }
}

/// Version of next which also eats preceding whitespace
bool Input::nextWS(const std::string& str)
{
    eatWS();
    return next(str);
}

/// Version of match which also eats preceding whitespace
bool Input::matchWS(const std::string& str)
{
    eatWS();
    return match(str);
}

/// Version of expect which eats preceding whitespace
void Input::expectWS(const std::string str)
{
    eatWS();
    expect(str);
}

// Forward declarations
ASTExpr* parseExprPrec(Input& input, int minPrec);
ASTExpr* parseExpr(Input& input);
ASTStmt* parseStmt(Input& input);
BlockStmt* parseBlockStmt(Input& input, std::string endStr);

/**
Parse a number
*/

FloatExpr* parseFloatingPart(Input& input, bool neg, char literal[64])
{
    int length = strlen(literal);
    for (int i = 0;;i++)
    {
        if (i + length >= 64)
            throw ParseError(input, "float literal is too long");
        char next = input.peekCh();
        if (isdigit(next) || next == 'e' || next == '.')
            literal[length + i] = input.readCh();
        else
            break;
    }
    input.expect("f");
    float floatVal = atof(literal);
    if (neg)
    {
        floatVal *= -1;
    }
    return new FloatExpr(floatVal);
}

ASTExpr* parseNum(Input& input, bool neg)
{
    char literal[64] = {0};

    for (int i = 0;;i++)
    {
        // Peek at the next character
        char ch = input.readCh();

        if (!isdigit(ch))
            throw ParseError(input, "expected digit");

        literal[i] = ch;

        // If the next character is not a digit, stop
        if (!isdigit(input.peekCh()))
            break;
    }

    if (input.peekCh() == '.' || input.peekCh() == 'e')
    {
        return parseFloatingPart(input, neg, literal);
    }
    int intVal = atoi(literal);
    // If the value is negative
    if (neg)
    {
        intVal *= -1;
    }

    return new IntExpr(intVal);
}

/**
Parse a string literal
*/
ASTExpr* parseStringLit(Input& input, char endCh)
{
    //std::cout << "parseStringLit" << std::endl;

    std::string str;

    for (;;)
    {
        // If this is the end of the input
        if (input.eof())
        {
            throw ParseError(
                input,
                "end of input inside string literal"
            );
        }

        // Consume this character
        char ch = input.readCh();

        // If this is the end of the string
        if (ch == endCh)
        {
            break;
        }

        // Disallow newlines inside strings
        if (ch == '\r' || ch == '\n')
        {
            throw ParseError(
                input,
                "newline character in string literal"
            );
        }

        // If this is an escape sequence
        if (ch == '\\')
        {
            char esc = input.readCh();

            switch (esc)
            {
                case 'n':   ch = '\n'; break;
                case 'r':   ch = '\r'; break;
                case 't':   ch = '\t'; break;
                case '0':   ch = '\0'; break;
                case '\'':  ch = '\''; break;
                case '\"':  ch = '\"'; break;
                case '\\':  ch = '\\'; break;

                // Hexadecimal escape
                case 'x':
                {
                    int escVal = 0;
                    for (size_t i = 0; i < 2; ++i)
                    {
                        auto ch = input.readCh();

                        if (ch >= '0' && ch <= '9')
                        {
                            escVal = 16 * escVal + (ch - '0');
                        }
                        else if (ch >= 'A' && ch <= 'F')
                        {
                            escVal = 16 * escVal + (ch - 'A' + 10);
                        }
                        else
                        {
                            throw ParseError(
                                input,
                                "invalid hexadecimal character escape code"
                            );
                        }
                    }

                    assert (escVal >= 0 && escVal <= 255);
                    ch = (char)escVal;
                }
                break;

                default:
                throw ParseError(
                    input,
                    "invalid character escape sequence"
                );
            }
        }

        str += ch;
    }

    return new StringExpr(str);
}

/**
Parse an identifier string
*/
std::string parseIdentStr(Input& input)
{
    std::string ident;

    char firstCh = input.peekCh();

    if (firstCh != '_' && !isalpha(firstCh))
        throw ParseError(input, "invalid identifier start");

    for (;;)
    {
        // Peek at the next character
        char ch = input.peekCh();

        if (!isalnum(ch) && ch != '_')
            break;

        // Consume this character
        ident += input.readCh();
    }

    if (ident.size() == 0)
        throw ParseError(input, "invalid identifier");

    return ident;
}

/**
Parse an if statement
if <test_expr> then <then_expr> else <else_expr>
*/
IfStmt* parseIfStmt(Input& input)
{
    input.expectWS("(");
    ASTExpr* testExpr = parseExpr(input);
    input.expectWS(")");

    ASTStmt* thenStmt = parseStmt(input);

    // Parse the else clause, if there is one
    ASTStmt* elseStmt = nullptr;
    if (input.matchWS("else"))
    {
        elseStmt = parseStmt(input);
    }
    else
    {
        elseStmt = new BlockStmt(std::vector<ASTStmt*>());
    }

    return new IfStmt(testExpr, thenStmt, elseStmt);
}

/**
Parse a for loop statement
*/
ForStmt* parseForStmt(Input& input)
{
    input.expectWS("(");

    ASTStmt* initStmt;
    if (input.matchWS(";"))
    {
        initStmt = new ExprStmt(new IdentExpr("true"));
    }
    else
    {
        // Parse the init statement
        initStmt = parseStmt(input);
        //if (cast(VarStmt)initStmt is null && cast(ExprStmt)initStmt is null)
        //    throw ParseError("invalid for-loop init statement", initStmt.pos);
    }

    // Parse the test expression
    ASTExpr* testExpr;
    if (input.matchWS(";"))
    {
        testExpr = new IdentExpr("true");
    }
    else
    {
        testExpr = parseExpr(input);
        input.expectWS(";");
    }

    // Parse the inccrement expression
    ASTExpr* incrExpr;
    if (input.matchWS(")"))
    {
        incrExpr = new IdentExpr("true");
    }
    else
    {
        incrExpr = parseExpr(input);
        input.expectWS(")");
    }

    // Parse the loop body
    auto bodyStmt = parseStmt(input);

    return new ForStmt(initStmt, testExpr, incrExpr, bodyStmt);
}

/**
Parse a try-catch statement
*/
TryStmt* parseTryStmt(Input& input)
{
    auto bodyStmt = parseStmt(input);

    input.expectWS("catch");
    input.expectWS("(");
    auto catchVar = parseIdentStr(input);
    input.expectWS(")");

    auto catchStmt = parseStmt(input);

    return new TryStmt(bodyStmt, catchStmt, catchVar);
}

/**
Parse a list of expressions
*/
std::vector<ASTExpr*> parseExprList(Input& input, std::string endStr)
{
    std::vector<ASTExpr*> exprs;

    // Until the end of the list
    for (;;)
    {
        // If this is the end of the list
        if (input.matchWS(endStr))
        {
            break;
        }

        // Parse an expression
        auto expr = parseExpr(input);
        exprs.push_back(expr);

        // If this is the end of the list
        if (input.matchWS(endStr))
        {
            break;
        }

        // If this is not the first element, there must be a separator
        input.expectWS(",");
    }

    return exprs;
}

/**
Parse an object literal expression
*/
ASTExpr* parseObjExpr(Input& input)
{
    std::vector<std::string> fieldNames;
    std::vector<ASTExpr*> valExprs;

    // Until the end of the list
    for (;;)
    {
        // If this is the end of the list
        if (input.matchWS("}"))
        {
            break;
        }

        // Parse the property name
        auto ident = parseIdentStr(input);

        input.expectWS(":");

        // Parse an expression
        auto expr = parseExpr(input);

        fieldNames.push_back(ident);
        valExprs.push_back(expr);

        // If this is the end of the list
        if (input.matchWS("}"))
        {
            break;
        }

        // If this is not the first element, there must be a separator
        input.expectWS(",");
    }

    return new ObjectExpr(fieldNames, valExprs);
}

/**
Parse a function (closure) expression
function (x,y,z) <body_expr>
*/
FunExpr* parseFunExpr(Input& input)
{
    // If a function name was specified
    std::string name;
    if (!input.nextWS("("))
    {
        name = parseIdentStr(input);
    }

    input.expectWS("(");

    std::vector<std::string> params;

    // Until the end of the argument list
    for (;;)
    {
        // If this is the end of the list
        if (input.matchWS(")"))
            break;

        // Parse a parameter name
        auto identStr = parseIdentStr(input);
        params.push_back(identStr);

        // If this is the end of the list
        if (input.matchWS(")"))
            break;

        // If this is not the first element, there must be a separator
        input.expect(",");
    }

    // Parse the function body
    input.expectWS("{");
    ASTStmt* body = parseBlockStmt(input, "}");

    return new FunExpr(name, body, params);
}

/**
Try to match an operator in the input
*/
const OpInfo* matchOp(Input& input, int minPrec, bool preUnary)
{
    char ch = input.peekCh();

    const OpInfo* op = nullptr;

    // Switch on the first character of the operator
    // We do this to avoid a long cascade of match tests
    switch (ch)
    {
        case '.':
        op = &OP_MEMBER;
        break;

        case '[':
        op = &OP_INDEX;
        break;

        case '(':
        op = &OP_CALL;
        break;

        case ':':
        if (input.next("::")) { op = &OP_OBJ_EXT; break; }
        if (input.next(":")) { op = &OP_M_CALL; break; }
        break;

        case '*':
        op = &OP_MUL;
        break;

        case '/':
        op = &OP_DIV;
        break;

        case '%':
        op = &OP_MOD;
        break;

        case '+':
        if (input.next("+")) { op = &OP_ADD; break; }
        break;

        case '-':
        op = preUnary? &OP_NEG:&OP_SUB;
        break;

        case '<':
        if (input.next("<=")) { op = &OP_LE; break; }
        if (input.next("<<")) { op = &OP_BIT_SHL; break; }
        if (input.next("<")) { op = &OP_LT; break; }
        break;

        case '>':
        if (input.next(">=")) { op = &OP_GE; break; }
        if (input.next(">>>")) { op = &OP_BIT_USHR; break; }
        if (input.next(">>")) { op = &OP_BIT_SHR; break; }
        if (input.next(">")) { op = &OP_GT; break; }
        break;

        case 'i':
        if (input.next("instanceof")) { op = &OP_INSTOF; break; }
        if (input.next("in")) { op = &OP_IN; break; }
        break;

        case '=':
        if (input.next("==")) { op = &OP_EQ; break; }
        if (input.next("=")) { op = &OP_ASSIGN; break; }
        break;

        case '!':
        if (input.next("!=")) { op = &OP_NE; break; }
        if (input.next("!")) { op = &OP_NOT; break; }
        break;

        case '|':
        if (input.next("||")) { op = &OP_OR; break; }
        if (input.next("|")) { op = &OP_BIT_OR; break; }
        break;

        case '&':
        if (input.next("&&")) { op = &OP_AND; break; }
        if (input.next("&")) { op = &OP_BIT_AND; break; }
        break;

        case '^':
        op = &OP_BIT_XOR;
        break;

        case '~':
        op = &OP_BIT_NOT;
        break;

        case 't':
        if (input.next("typeof")) { op = &OP_TYPEOF; break; }
        break;
    }

    // If any operator was found
    if (op)
    {
        //std::cout << "op found: " << op->str << std::endl;

        // If its precedence isn't high enough or it doesn't meet
        // the arity and associativity requirements
        if ((op->prec < minPrec) ||
            (preUnary && op->arity != 1) ||
            (preUnary && op->assoc != 'r'))
        {
            return nullptr;
        }

        // Match the operator string
        bool matched = input.match(op->str);
        assert (matched);
    }

    // Return the matched operator, if any
    return op;
}

/**
Parse an atomic expression
*/
ASTExpr* parseAtom(Input& input)
{
    //printf("parseAtom\n");

    // Consume whitespace
    input.eatWS();

    // Numerical constant
    if (isdigit(input.peekCh()))
    {
        return parseNum(input, false);
    }

    // String literal
    if (input.match("\'"))
    {
        return parseStringLit(input, '\'');
    }
    if (input.match("\""))
    {
        return parseStringLit(input, '\"');
    }

    // Array literal
    if (input.match("["))
    {
        return new ArrayExpr(parseExprList(input, "]"));
    }

    // Object literal
    if (input.match("{"))
    {
        return parseObjExpr(input);
    }

    // Parenthesized expression
    if (input.match("("))
    {
        auto expr = parseExpr(input);

        input.expectWS(")");

        return expr;
    }

    // Try matching a right-associative (prefix) unary operators
    auto op = matchOp(input, 0, true);

    // If a matching operator was found
    if (op)
    {
        auto expr = parseExprPrec(input, op->prec);
        return new UnOpExpr(op, expr);
    }

    // Identifier
    if (isalnum(input.peekCh()))
    {
        // Function expression
        if (input.match("function"))
        {
            return parseFunExpr(input);
        }

        if (input.match("import"))
        {
            auto pkgExpr = parseAtom(input);
            auto strExpr = dynamic_cast<StringExpr*>(pkgExpr);

            if (!strExpr)
                throw ParseError(input, "expected string literal");

            return new ImportExpr(strExpr->val);
        }

        // Identifier, variable reference
        return new IdentExpr(parseIdentStr(input));
    }

    // Inline IR expression
    if (input.match("$"))
    {
        auto opName = parseIdentStr(input);
        input.expect("(");
        auto argExprs = parseExprList(input, ")");
        return new IRExpr(opName, argExprs);
    }

    // Parsing failed
    throw ParseError(input, "invalid expression");
}

/**
Parse an expression using the precedence climbing algorithm
*/
ASTExpr* parseExprPrec(Input& input, int minPrec)
{
    // The first call has min precedence 0
    //
    // Each call loops to grab everything of the current precedence or
    // greater and builds a left-sided subtree out of it, associating
    // operators to their left operand
    //
    // If an operator has less than the current precedence, the loop
    // breaks, returning us to the previous loop level, this will attach
    // the atom to the previous operator (on the right)
    //
    // If an operator has the mininum precedence or greater, it will
    // associate the current atom to its left and then parse the rhs

    // Parse the first atom
    auto lhsExpr = parseAtom(input);

    for (;;)
    {
        // Consume whitespace
        input.eatWS();

        //printf("looking for op, minPrec=%d\n", minPrec);

        // Attempt to match an operator in the input
        // with sufficient precedence
        auto op = matchOp(input, minPrec, false);

        // If no operator matches, break out
        if (op == nullptr)
            break;

        //printf("found op: %s\n", op->str);
        //printf("op->prec=%d, minPrec=%d\n", op->prec, minPrec);

        // Compute the minimum precedence for the recursive call (if any)
        int nextMinPrec;
        if (op->assoc == 'l')
        {
            if (op->closeStr.length() > 0)
                nextMinPrec = 0;
            else
                nextMinPrec = (op->prec + 1);
        }
        else
        {
            nextMinPrec = op->prec;
        }

        // If this is a regular function call expression
        if (op == &OP_CALL)
        {
            // Parse the argument list and create the call expression
            auto argExprs = parseExprList(input, ")");

            lhsExpr = new CallExpr(lhsExpr, argExprs);
        }

        // If this is a method call expression
        else if (op == &OP_M_CALL)
        {
            // Parse the identifier string
            auto identStr = parseIdentStr(input);

            // Parse the argument list and create the call expression
            input.expect("(");
            auto argExprs = parseExprList(input, ")");

            lhsExpr = new MethodCallExpr(
                lhsExpr,
                identStr,
                argExprs
            );
        }

        // If this is a member expression
        else if (op == &OP_MEMBER)
        {
            // Parse the identifier string
            auto identStr = parseIdentStr(input);

            // Produce an indexing expression
            lhsExpr = new BinOpExpr(
                op,
                lhsExpr,
                new IdentExpr(identStr)
            );
        }

        // If this is a binary operator
        else if (op->arity == 2)
        {
            // If the operator can be folded into an assignment expression
            input.eatWS();
            if (op->foldAssign && input.match("="))
            {
                // Recursively parse the rhs
                auto rhsExpr = parseExprPrec(input, OP_ASSIGN.prec + 1);

                // Create an assignment expression
                lhsExpr = new BinOpExpr(
                    &OP_ASSIGN,
                    lhsExpr,
                    new BinOpExpr(
                        op,
                        lhsExpr,
                        rhsExpr
                    )
                );
            }
            else
            {
                // Recursively parse the rhs
                auto rhsExpr = parseExprPrec(input, nextMinPrec);

                // Create a new parent node for the expressions
                lhsExpr = new BinOpExpr(
                    op,
                    lhsExpr,
                    rhsExpr
                );

                // If specified, match the operator closing string
                if (op->closeStr.length() > 0 && !input.matchWS(op->closeStr))
                    throw ParseError(input, "expected operator closing");
            }
        }

        else
        {
            // Unhandled operator
            std::cerr << "operator not handled correctly: " << op->str << std::endl;
            assert (false);
        }
    }

    // Return the parsed expression
    return lhsExpr;
}

/**
Recursively parse an expression, starting at the least precedence level
*/
ASTExpr* parseExpr(Input& input)
{
    return parseExprPrec(input, 0);
}

/**
Parse a block statement
*/
BlockStmt* parseBlockStmt(Input& input, std::string endStr)
{
    std::vector<ASTStmt*> stmts;

    // Until the end of the list
    for (;;)
    {
        // Read whitespace
        input.eatWS();

        // If this is the end of the block statement
        if ((endStr == "" && input.eof()) ||
            (endStr != "" && input.match(endStr)))
        {
            break;
        }

        // Parse a statement
        auto stmt = parseStmt(input);
        stmts.push_back(stmt);
    }

    return new BlockStmt(stmts);
}

ASTStmt* parseStmt(Input& input)
{
    // Consume whitespace
    input.eatWS();

    // Sequence/block expression (i.e { a; b; c }
    if (input.match("{"))
    {
        return parseBlockStmt(input, "}");
    }

    // Variable declaration
    if (input.match("var"))
    {
        input.eatWS();
        auto ident = parseIdentStr(input);

        input.expectWS("=");

        auto initExpr = parseExpr(input);

        if (auto funExpr = dynamic_cast<FunExpr*>(initExpr))
        {
            if (funExpr->name == "")
            {
                funExpr->name = ident;
            }
        }

        input.expectWS(";");

        return new VarStmt(ident, initExpr);
    }

    // If-else statement
    if (input.match("if"))
    {
        return parseIfStmt(input);
    }

    // For loop statement
    if (input.match("for"))
    {
        return parseForStmt(input);
    }

    // Return statement
    if (input.match("break"))
    {
        input.expectWS(";");
        return new BreakStmt();
    }

    // Return statement
    if (input.match("continue"))
    {
        input.expectWS(";");
        return new ContStmt();
    }

    // Try/catch statement
    if (input.match("try"))
    {
        return parseTryStmt(input);
    }

    // Return statement
    if (input.match("return"))
    {
        if (input.matchWS(";"))
            return new ReturnStmt(new IdentExpr("undef"));

        auto expr = parseExpr(input);
        input.expectWS(";");

        return new ReturnStmt(expr);
    }

    // Throw statement
    if (input.match("throw"))
    {
        auto expr = parseExpr(input);
        input.expectWS(";");
        return new ThrowStmt(expr);
    }

    // Assert statement
    if (input.match("assert"))
    {
        input.expectWS("(");

        ASTExpr* testExpr = parseExpr(input);

        ASTExpr* errMsg;
        if (input.matchWS(","))
            errMsg = parseExpr(input);
        else
            errMsg = new StringExpr("assertion failed");

        //Pretend to be similar to the self hosted implementation
        //by wrapping the msg in an object
        errMsg = new ObjectExpr(
            {"msg"}, {errMsg}
            );

        input.expectWS(")");
        input.expectWS(";");

        return new IfStmt(
            testExpr,
            new BlockStmt(std::vector<ASTStmt*>()),
            new ThrowStmt(errMsg)
        );
    }

    // Inline IR statement
    if (input.match("$"))
    {
        auto opName = parseIdentStr(input);
        input.expect("(");
        auto argExprs = parseExprList(input, ")");

        input.expectWS(";");

        return new IRStmt(opName, argExprs);
    }

    // Expression statement
    auto expr = parseExpr(input);
    input.expectWS(";");
    return new ExprStmt(expr);
}

/**
Parse a source unit from an input object
*/
FunExpr* parseUnit(Input& input)
{
    // Parse the language directive, if specified
    if (input.match("#language"))
    {
        input.expectWS("\"");

        for (;;)
        {
            // If this is the end of the input
            if (input.eof())
            {
                throw ParseError(
                    input,
                    "end of input inside package name"
                );
            }

            char ch = input.readCh();

            if (ch == '"')
            {
                break;
            }
        }
    }

    ASTStmt* blockStmt = parseBlockStmt(input, "");

    // Create the top-level function
    return new FunExpr("unit", blockStmt, std::vector<std::string>());
}

/**
Parse a source string as a unit
*/
FunExpr* parseString(std::string str, std::string srcName)
{
    Input input(
        str,
        srcName
    );

    return parseUnit(input);
}

FunExpr* parseFile(std::string fileName)
{
    auto fileData = readFile(fileName);

    Input input(
        fileData,
        fileName
    );

    return parseUnit(input);
}

/// Test that the parsing of a string succeeds
FunExpr* testParse(std::string str)
{
    std::cout << str << std::endl;

    try
    {
        return parseString(str, "parser_test");
    }

    catch (std::exception& e)
    {
        std::cout << e.what() << std::endl;
        exit(-1);
    }
}

/// Test that the parsing of a string fails
void testParseFail(std::string str)
{
    std::cout << str << std::endl;

    try
    {
        parseString(str, "parser_fail_test");
    }

    catch (ParseError e)
    {
        return;
    }

    std::cout << "parsing did not fail for: " << std::endl;
    std::cout << str << std::endl;
    exit(-1);
}

/// Test that the parsing of a file succeeds
ASTNode* testParseFile(std::string fileName)
{
    std::cout << "parsing image file \"" << fileName << "\"" << std::endl;
    return parseFile(fileName);
}

/// Test the functionality of the parser
void testParser()
{
    printf("parser tests\n");

    // Identifiers
    testParse("foobar;");
    testParse("  foo_bar;  ");
    testParse("  foo_bar ; ");

    // Literals
    testParse("123;");
    testParse("'abc';");
    testParse("\"double-quoted string!\";");
    testParse("\"double-quoted string, 'hi'!\";");
    testParse("'hi'; // comment");
    testParse("'hi';");
    testParse("'new\\nline';");
    testParse("true;");
    testParse("false;");
    testParseFail("'invalid\\iesc'");
    testParseFail("'str' []");

    // Array literals
    testParse("[];");
    testParse("[1];");
    testParse("[1,a];");
    testParse("[1 , a];");
    testParse("[1,a, ];");
    testParse("[ 1,\na ];");
    testParseFail("[,];");

    // Object literals
    testParse("x = {x:3};");
    testParse("x = {x:3,y:2};");
    testParse("x = {x:3,y:2+z};");
    testParse("x = {x:3,y:2+z,};");
    testParseFail("x = {,};");

    // Comments
    testParse("1; // comment");
    testParse("[ 1//comment\n,a ];");
    testParse("1 /* comment */ + x;");
    testParse("1 /* // comment */ + x;");
    testParseFail("1; // comment\n#1");
    testParseFail("1; /* */ */");

    // Unary and binary expressions
    testParse("-1;");
    testParse("-x + 2;");
    testParse("x + -1;");
    testParse("a + b;");
    testParse("a + b + c;");
    testParse("a + b - c;");
    testParse("a + b * c + d;");
    testParse("a || b || c;");
    testParse("(a);");
    testParse("(b ) ;");
    testParse("(a + b);");
    testParse("(a + (b + c));");
    testParse("((a + b) + c);");
    testParse("(a + b) * (c + d);");
    testParseFail("*a;");
    testParseFail("a*;");
    testParseFail("a # b;");
    testParseFail("a +;");
    testParseFail("a + b # c;");
    testParseFail("(a;");
    testParseFail("(a + b));");
    testParseFail("((a + b);");

    // Member expression
    testParse("a.b;");
    testParse("a.b + c;");
    testParseFail("a. b;");
    testParseFail("a.'b';");

    // Array indexing
    testParse("a[0];");
    testParse("a[b];");
    testParse("a[b+2];");
    testParse("a[2*b+1];");
    testParseFail("a[];");
    testParseFail("a[0 1];");

    // Object extension
    testParse("x = o::{x:3};");

    // If statement
    testParse("if (1) 2; ");
    testParse("if (1) return 2;");
    testParse("if (!x) return 1; else return 2;");
    testParse("if (!x && y) return 1;");
    testParse("if (x <= 2) return 0; ");
    testParse("if (x == 1) return 2; ");
    testParse("if (typeof x == 'string') return 2; ");
    testParse("if ('foo' in obj) return 1;");

    // For loop
    testParse("for (a; b; c) d;");
    testParse("for (;;) d;");
    testParse("for (i = 0; i < 10; i = i + 1) x;");

    // Assignment
    testParse("x = 1;");
    testParse("x = -1;");
    testParse("a.b = x + y;");
    testParse("x = y = 1;");
    testParse("var x = 3;");
    testParseFail("var");
    testParseFail("let");
    testParseFail("let x");
    testParseFail("let x=");
    testParseFail("var +");
    testParseFail("var 3");

    // Call expressions
    testParse("a();");
    testParse("a(b);");
    testParse("a(b,c);");
    testParse("a(b,c+1);");
    testParse("a(b,c+1,);");
    testParse("x + a(b,c+1);");
    testParse("x + a(b,c+1) + y;");
    testParse("a(); b();");
    testParseFail("a(b c+1);");

    // Package import
    testParse("var io = import 'core/io';");

    // Inline IR
    testParse("var s = $add_i32(1, 2);");
    testParse("$array_push(arr, val);");

    // Assert statement
    testParse("assert(x);");
    testParse("assert(x, 'foo');");
    testParseFail("assert(x, 'foo', z);");

    // Function expression
    testParse("function () { return 0; }; ");
    testParse("function (x) {return x;};");
    testParse("function foo(x) { return x; };");
    testParse("function (x,y) { return x; };");
    testParse("function (x,y,) { return x; };");
    testParse("function (x,y) { return x+y; };");
    testParse("obj.method = function (this, x) { this.x = x; }; ");
    testParseFail("function (x,y)");

    // Sequence/block expression
    testParse("{ 1; 2; }");
    testParse("function (x) { print(x); print(y); };");
    testParse("function (x) { var y = x + 1; print(y); };");
    testParseFail("{ a, }");
    testParseFail("{ a, b }");
    testParseFail("function foo () { a, };");

    // Method calls
    testParse("o:foo();");
    testParse("o:foo(1,2);");
    testParse("x + o:foo(1,2);");
    testParseFail("x + o:foo (1,2);");
    testParseFail("x + o:foo (1,2);");
    testParseFail("(o:foo)();");

    // Try, catch and throw
    testParse("throw x;");
    testParse("try {} catch (e) {}");

    // There is no empty statement
    testParseFail(";");

    // Regressions
    testParse("-1;");
    testParseFail("'a' <'");
}
