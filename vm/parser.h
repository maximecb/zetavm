#pragma once

#include <cstdio>
#include <string>
#include <exception>
#include "runtime.h"

/**
Represents an input character stream to parse from
*/
struct Input
{
private:

    /// Input source name
    std::string srcName;

    /// Input string to be parsed
    std::string inStr;

    /// Current index in the input string
    size_t strIdx;

    /// Current line number
    size_t lineNo;

    /// Current column number
    size_t colNo;

public:

    Input(std::string fileName);

    Input(std::string str, std::string srcName);

    ~Input();

    /// Read/consume a character from the input
    char readCh();

    /// Test if the end of file has been reached
    bool eof();

    /// Peek at a character from the input
    char peek();

    /// Peek to see if a specific character is next in the input
    bool peek(char ch);

    /// Peek to check if a string is next in the input
    bool peek(const std::string& str);

    /// Try and match a given character in the input
    /// The character is consumed if matched
    bool match(char ch);

    /// Try and match a given string in the input
    /// The string is consumed if matched
    bool match(const std::string& str);

    /// Fail if the input doesn't match a given string
    void expect(const std::string str);

    /// Consume whitespace and comments
    void eatWS();

    /// Get the entire input as a string
    std::string getInputStr() const { return inStr; }

    /// Get the current index in the input
    size_t getInputIdx() const { return strIdx; }

    std::string getSrcName() const { return srcName; }
    size_t getLineNo() const { return lineNo; }
    size_t getColNo() const { return colNo; }
};

/**
Parse-time error exception class
*/
class ParseError : public RunError
{
public:

    ParseError(std::string msg) : RunError(msg)
    {
        assert (msg.length() > 0);
    }

    ParseError(Input& input, std::string msg) :
        RunError(
            input.getSrcName() + "@" +
            std::to_string(input.getLineNo()) + ":" +
            std::to_string(input.getColNo()) + " - " +
            msg
        )
    {
    }

    virtual ~ParseError()
    {
    }
};

// Parse the optional language directive at the beginning of a file
std::string parseLang(Input& input);

// Parse the contents of some input object (ZIM format)
Value parseInput(Input& input);

// Parse a plain image file (ZIM format)
Value parseFile(std::string fileName);

// Parse a string (ZIM format)
Value parseString(std::string str, std::string srcName);

void testParser();
