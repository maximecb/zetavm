#include <cassert>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include "runtime.h"
#include "parser.h"

/// Read an entire file at once
std::string readFile(std::string fileName)
{
    FILE* file = fopen(fileName.c_str(), "rb");

    if (!file)
    {
        throw ParseError(
            "failed to open file \"" + fileName + "\""
        );
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

Input::Input(std::string fileName)
: Input(readFile(fileName), fileName)
{
    // Note: if we ever need/want to implement parallel
    // parsing, we could mmap the file instead of
    // reading it into memory all at once
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

/// Read a character from the input
char Input::readCh()
{
    char ch = peek();

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
    return peek('\0');
}

/// Peek at a character from the input
char Input::peek()
{
    if (strIdx >= inStr.length())
        return '\0';

    return inStr[strIdx];
}

/// Peek to see if a specific character is next in the input
bool Input::peek(char ch)
{
    return peek() == ch;
}

/// Peek to check if a string is next in the input
bool Input::peek(const std::string& str)
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

/// Try and match a given character in the input
/// The character is consumed if matched
bool Input::match(char ch)
{
    if (peek(ch))
    {
        readCh();
        return true;
    }

    return false;
}

/// Try and match a given string in the input
/// The string is consumed if matched
bool Input::match(const std::string& str)
{
    if (peek(str))
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
        if (isspace(peek()))
        {
            readCh();
            continue;
        }

        // If this is a single-line comment
        if (match('#'))
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

        // This isn't whitespace, stop
        break;
    }
}

// Forward declaration
Value parseExpr(Input& input);

Value parseFloatingPart(Input& input, bool neg, char literal[64]);

/**
Parse a decimal integer
*/
Value parseNum(Input& input, bool neg)
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
        if (!isdigit(input.peek()))
            break;
    }

    char next = input.peek();
    if (next == '.' || next == 'e')
    {
        return parseFloatingPart(input, neg, literal);
    }
    int intVal = atoi(literal);
    // If the value is negative
    if (neg)
    {
        intVal *= -1;
    }

    return Value::int32(intVal);
}

Value parseFloatingPart(Input& input, bool neg, char literal[64])
{
    int length = strlen(literal);
    for (int i = 0;;i++)
    {
        if (i + length >= 64)
            throw ParseError(input, "float literal is too long");
        char next = input.peek();
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
    return Value::float32(floatVal);
}

/**
Parse a string literal
*/
std::string parseStringLit(Input& input, char endCh)
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

    return str;
}

/**
Parse an identifier string
*/
std::string parseIdentStr(Input& input)
{
    std::string ident;

    char firstCh = input.peek();

    if (firstCh != '_' && !isalpha(firstCh))
        throw ParseError(input, "invalid identifier start");

    for (;;)
    {
        // Peek at the next character
        char ch = input.peek();

        if (!isalnum(ch) && ch != '_')
            break;

        // Consume this character
        ident += input.readCh();
    }

    if (ident.size() == 0)
        throw ParseError(input, "invalid identifier");

    // Sanity check on the parsed identifier string
    assert (isValidIdent(ident));

    return ident;
}

/**
Parse a list of expressions
*/
std::vector<Value> parseExprList(Input& input, char endCh)
{
    std::vector<Value> exprs;

    // Until the end of the list
    for (;;)
    {
        // Read whitespace
        input.eatWS();

        // If this is the end of the list
        if (input.match(endCh))
        {
            break;
        }

        // Parse an expression
        auto expr = parseExpr(input);

        // Add the expression to the array
        exprs.push_back(expr);

        // Read whitespace
        input.eatWS();

        // If this is the end of the list
        if (input.match(endCh))
        {
            break;
        }

        // If this is not the first element, there must be a separator
        input.expect(",");
    }

    return exprs;
}

/**
Parse an array literal
*/
Value parseArray(Input& input)
{
    auto exprVals = parseExprList(input, ']');

    // Allocate an array
    auto array = Array(exprVals.size());

    // Write the elements in the array
    for (auto exprVal : exprVals)
        array.push(exprVal);

    return array;
}

/**
Parse an object literal
*/
Value parseObject(Input& input)
{
    // Allocate an empty object
    Object obj = Object();

    // Until the end of the list
    for (;;)
    {
        // Consume whitespace
        input.eatWS();

        // If this is the end of the list
        if (input.match('}'))
        {
            break;
        }

        // Parse the field name
        std::string fieldName;

        if (input.peek('"'))
        {
            input.readCh();
            fieldName = parseStringLit(input, '"');
        }
        else if (input.peek('\''))
        {
            input.readCh();
            fieldName = parseStringLit(input, '\'');
        }
        else
        {
            fieldName = parseIdentStr(input);
        }

        input.eatWS();
        input.expect(":");

        // Parse an expression
        auto expr = parseExpr(input);

        // Set the property on the object
        obj.setField(fieldName, expr);

        assert (obj.hasField(fieldName));

        // If this is the end of the list
        input.eatWS();
        if (input.match('}'))
        {
            break;
        }

        // If this is not the first element, there must be a separator
        input.expect(",");
    }

    return obj;
}

/**
Parse a top-level expression
*/
Value parseExpr(Input& input)
{
    //std::cout << "parseExpr" << std::endl;

    // Consume whitespace
    input.eatWS();

    // Peek at the current character
    auto ch = input.peek();

    // Numerical value
    if (isdigit(ch))
    {
        return parseNum(input, false);
    }

    // Negative number
    if (input.match('-'))
    {
        return parseNum(input, true);
    }

    // String literal
    if (input.match('\''))
    {
        return String(parseStringLit(input, '\''));
    }
    if (input.match('\"'))
    {
        return String(parseStringLit(input, '\"'));
    }

    // Array expression
    if (input.match('['))
    {
        return parseArray(input);
    }

    // Object literal
    if (input.match('{'))
    {
        return parseObject(input);
    }

    // Global value reference
    // ie: @foo
    if (input.match('@'))
    {
        // Produce an image reference placeholder
        return ImgRef(parseIdentStr(input));
    }

    // Special values
    if (input.match('$'))
    {
        // Special "undefined" value
        if (input.match("undef"))
            return Value::UNDEF;

        // Booleans true and false
        if (input.match("true"))
            return Value::TRUE;
        if (input.match("false"))
            return Value::FALSE;

        throw ParseError(
            input,
            "unrecognized special value"
        );
    }

    // If the end of file was reached
    if (input.eof())
    {
        throw ParseError(
            input,
            "end of input reached when expecting expression"
        );
    }

    std::string chStr = std::string("'") + ch + "'";
    char hexStr[64];
    sprintf(hexStr, "0x%02X", (int)ch);

    // Parsing failed
    throw ParseError(
        input,
        "unrecognized expression starting with char " +
        (isprint(ch)?
            (chStr + " (" + hexStr + ")"):
            (hexStr)
        )
    );
}

/**
Process a value which is potentially a reference
*/
Value processRef(
    std::unordered_map<std::string, Value>& globalDefs,
    std::vector<Value>& visitStack,
    Value val
)
{
    // If this is a reference
    if (val.getTag() == TAG_IMGREF)
    {
        auto ref = ImgRef(val);
        auto varName = ref.getName();

        // Get the value of this reference
        auto refVal = globalDefs.find(varName);

        if (refVal == globalDefs.end())
        {
            throw ParseError(
                "unresolved reference to \"" + varName + "\""
            );
        }

        assert (refVal->second.getTag() != TAG_IMGREF);

        // Visit the value being reffered to
        visitStack.push_back(refVal->second);

        return refVal->second;
    }
    else
    {
        visitStack.push_back(val);

        return val;
    }
}

/**
Resolve the references in values exported by the image
*/
Value resolveRefs(
    std::unordered_map<std::string, Value>& globalDefs,
    Value exportsTree
)
{
    // Set of visited nodes
    std::unordered_set<refptr> visited;

    // Stack of nodes to visit
    std::vector<Value> stack;

    // The root node itself may be a reference, so we process it firstg
    exportsTree = processRef(globalDefs, stack, exportsTree);

    // Until we are done
    while (!stack.empty())
    {
        // Pop a node off the stack
        auto node = stack.back();
        stack.pop_back();

        // If this is an array or an object
        if (node.isPointer())
        {
            auto ptr = (refptr)node;

            // If this node was previously visited, skip it
            if (visited.find(ptr) != visited.end())
                continue;

            // Mark the node as visited
            visited.insert(ptr);
        }

        switch (node.getTag())
        {
            case TAG_ARRAY:
            {
                auto arr = Array(node);
                auto len = arr.length();

                // For each array element
                for (size_t i = 0; i < len; ++i)
                {
                    auto elemVal = arr.getElem(i);
                    auto newVal = processRef(globalDefs, stack, elemVal);
                    arr.setElem(i, newVal);
                }
            }
            break;

            case TAG_OBJECT:
            {
                auto obj = Object(node);

                // For each object field
                for (auto itr = ObjFieldItr(obj); itr.valid(); itr.next())
                {
                    auto fieldName = itr.get();
                    auto fieldVal = obj.getField(fieldName);

                    auto newVal = processRef(globalDefs, stack, fieldVal);

                    obj.setField(fieldName, newVal);
                    assert (obj.getField(fieldName).getTag() != TAG_IMGREF);
                }
            }
            break;

            case TAG_UNDEF:
            case TAG_BOOL:
            case TAG_INT32:
            case TAG_INT64:
            case TAG_FLOAT32:
            case TAG_STRING:
            continue;

            case TAG_IMGREF:
            assert (false);

            default:
            assert (false);
        }
    }

    return exportsTree;
}

Value parseInput(Input& input)
{
    // Global definitions
    std::unordered_map<std::string, Value> globalDefs;

    // Until done parsing all expressions
    for (;;)
    {
        input.eatWS();

        // If this is not an identifier, we are done parsing
        // global definitions (ie: foo = 1)
        if (input.peek() != '_' && !isalpha(input.peek()))
            break;

        std::string ident = parseIdentStr(input);

        // Match the assignment operator
        input.eatWS();
        input.expect("=");

        // Cannot assign a global def to another global def
        input.eatWS();
        if (input.match('@'))
        {
            throw ParseError(
                input,
                "cannot assign a global definition to another global definition"
            );
        }

        // Parse the right-hand expression
        auto defVal = parseExpr(input);

        // A global name can only be associated with one definition
        if (globalDefs.find(ident) != globalDefs.end())
        {
            throw ParseError(input, "redefinition of \"" + ident + "\"");
        }

        // Add the value to the global definitions map
        globalDefs[ident] = defVal;

        // Every top-level expression must end with a semicolon
        // This allows splitting the input without fully parsing it
        input.eatWS();
        input.expect(";");
    }

    // Parse the final expression. This is the value this image exports,
    // which is usually an object
    auto exports = parseExpr(input);

    input.eatWS();
    input.expect(";");

    // If there remains unparsed input
    input.eatWS();
    if (!input.eof())
    {
        throw ParseError(input, "unconsumed input remains");
    }

    // Resolve the global references in the image
    exports = resolveRefs(globalDefs, exports);

    // Return the last evaluated value
    return exports;
}

// Parse the optional hashbang line at the beginning of a file
void parseHashbang(Input& input)
{
    // Parse the hashbang line, if present
    if (input.match("#!"))
    {
        for (;;)
        {
            if (input.eof())
            {
                throw ParseError(input, "end of input in hashbang line");
            }

            if (input.readCh() == '\n')
            {
                break;
            }
        }
    }
}

// Parse the optional language directive at the beginning of a file
std::string parseLang(Input& input)
{
    parseHashbang(input);

    // If the language directive is not present, then the
    // zeta-image tag should be. This is so we can provide
    // sensible error messages when someone forgets to place
    // a language directive in a source file.
    if (input.match("#zeta-image"))
    {
        return "";
    }

    input.expect("#language");

    input.eatWS();

    std::string pkgName;

    input.expect("\"");

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

        // Consume this character
        char ch = input.readCh();

        // If this is the end of the string
        if (ch == '"')
        {
            break;
        }

        // Disallow newlines
        if (ch == '\r' || ch == '\n')
        {
            throw ParseError(
                input,
                "newline character in package name"
            );
        }

        pkgName += ch;
    }

    return pkgName;
}

Value parseFile(std::string fileName)
{
    Input input(fileName);

    // Parse the hashbang line, if present
    parseHashbang(input);

    input.expect("#zeta-image");

    return parseInput(input);
}

Value parseString(std::string str, std::string srcName)
{
    Input input(
        str,
        srcName
    );

    return parseInput(input);
}

/// Test that the parsing of a string succeeds
Value testParse(std::string str)
{
    std::cout << str << std::endl;

    try
    {
        return parseString(str, "parser_test");
    }

    catch (RunError& e)
    {
        std::cout << e.toString() << std::endl;
        exit(-1);
    }
}

/// Test that the parsing of a string succeeds
/// and that the type tag is the expected one
void testParse(std::string str, Tag expectTag)
{
    auto val = testParse(str);

    if (val.getTag() != expectTag)
    {
        std::cout << "incorrect tag for parse" << std::endl;
        exit(-1);
    }
}

/// Test that the parsing of a string fails
void testParseFail(std::string str)
{
    std::cout << str << std::endl;

    try
    {
        auto unit = parseString(str, "parser_fail_test");
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
Value testParseFile(std::string fileName)
{
    std::cout << "parsing image file \"" << fileName << "\"" << std::endl;
    return parseFile(fileName);
}

/// Test the proper functioning of the parser
void testParser()
{
    std::cout << "image parser tests" << std::endl;

    // Literals
    testParse("0;", TAG_INT32);
    testParse("123;");
    testParse("-1;");
    testParse("-127;");
    testParse(" 456   ;  ");
    testParse("$undef;", TAG_UNDEF);
    testParse("$true;", TAG_BOOL);
    testParse("$false;", TAG_BOOL);
    testParseFail("ident");
    testParseFail("-");
    testParseFail("-a");
    testParseFail("1 / 2");

    // String literals
    testParse("'abc';", TAG_STRING);
    testParse("\"def\";", TAG_STRING);
    testParse("'hi';");
    testParse("'new\\nline\\ttab';");
    testParse("'hi\\x20you\\x21';");
    testParse("'\\x00';");
    testParse("'\\xFF';");
    testParseFail("'\\x!!';");
    testParseFail("'\\xG0';");
    testParseFail("'\\x0G';");
    testParseFail("'test invalid\\iescape seq'");
    testParseFail("'foo");

    // Array literals
    testParse("[];", TAG_ARRAY);
    testParse("[1];");
    testParse("[1,2];");
    testParse("[1 , 2];");
    testParse("[1,'foo', ];");
    testParse("[ 1,\n'foo' ];");
    testParseFail("[,];");

    // Object literals
    testParse("{};", TAG_OBJECT);
    testParse("{a:1};");
    testParse("{p: 1};");
    testParse("{ a:1 };");
    testParse("{ a:2, };");
    testParse("{ a:1, b:2 };");
    testParse("{ name:'foo', list:[1,2,3] };");
    testParse("{a:1, b:2, c : 'foo' };");
    testParseFail("{ a:1 b:2 };");
    testParseFail("{ a };");

    // Comments
    testParse("1;# hi");
    testParse("1; # comment");
    testParse("[ 1# comment\n,2 ];");
    testParseFail("1; /* comment */");
    testParseFail("1; # comment\n!1");

    // Global definitions
    testParse("x = 1; 1;", TAG_INT32);
    testParse("y=2; 2;");
    testParse("x=1; y=2; 2;");
    testParse("foo = 'bar'; 1;");
    testParse("x = 1 ; 'def';", TAG_STRING);
    testParseFail("'abc'; x = 1; 1;");
    testParseFail("x=y=2;");
    testParseFail("x=1; x=2;");
    testParseFail("x=1 y=2;");

    // Global value references
    testParse("x = 1; @x;", TAG_INT32);
    testParse("x = 1; y = 2; [@x, @y, 3];", TAG_ARRAY);
    testParseFail("x = 1; y = @x; @x");

    // Parse test image files
    testParseFile("tests/vm/ex_image2.zim");
    testParseFile("tests/vm/ex_image.zim");
    testParseFile("tests/vm/ex_ret_cst.zim");
    testParseFile("tests/vm/ex_loop_cnt.zim");
    testParseFile("tests/vm/ex_rec_fact.zim");
    testParseFile("tests/vm/ex_fibonacci.zim");
}
