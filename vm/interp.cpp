#include <cassert>
#include <iostream>
#include <unordered_map>
#include "runtime.h"
#include "parser.h"
#include "interp.h"
#include "packages.h"
#include <math.h>
#include <unordered_set>

/// Opcode enumeration
enum Opcode : uint16_t
{
    // Local variable access
    GET_LOCAL,
    SET_LOCAL,

    // Stack manipulation
    PUSH,
    POP,
    DUP,
    SWAP,

    // 32-bit integer operations
    ADD_I32,
    SUB_I32,
    MUL_I32,
    DIV_I32,
    MOD_I32,
    SHL_I32,
    SHR_I32,
    USHR_I32,
    AND_I32,
    OR_I32,
    XOR_I32,
    NOT_I32,
    LT_I32,
    LE_I32,
    GT_I32,
    GE_I32,
    EQ_I32,
    INC_I32,
    DEC_I32,

    // Floating-point operations
    ADD_F32,
    SUB_F32,
    MUL_F32,
    DIV_F32,
    LT_F32,
    LE_F32,
    GT_F32,
    GE_F32,
    EQ_F32,
    SIN_F32,
    COS_F32,
    SQRT_F32,
    LOG_F32,
    EXP_F32,

    // Conversion operations
    I32_TO_F32,
    I32_TO_STR,
    F32_TO_I32,
    F32_TO_STR,
    STR_TO_F32,

    // Miscellaneous
    EQ_BOOL,
    HAS_TAG,
    GET_TAG,
    LOCAL_HAS_TAG,

    // String operations
    STR_LEN,
    GET_CHAR,
    GET_CHAR_CODE,
    CHAR_TO_STR,
    STR_CAT,
    EQ_STR,

    // Object operations
    NEW_OBJECT,
    HAS_FIELD,
    SET_FIELD,
    GET_FIELD,
    GET_FIELD_IMM,
    GET_FIELD_LIST,
    EQ_OBJ,

    // Array operations
    NEW_ARRAY,
    ARRAY_LEN,
    ARRAY_PUSH,
    ARRAY_POP,
    GET_ELEM,
    SET_ELEM,
    EQ_ARRAY,

    // Branch instructions
    JUMP,
    JUMP_STUB,
    IF_TRUE,
    CALL,
    RET,
    THROW
};

class CodeFragment
{
public:

    /// Start index in the executable heap
    uint8_t* startPtr = nullptr;

    /// End index in the executable heap
    uint8_t* endPtr = nullptr;

    /// Get the length of the code fragment
    size_t length()
    {
        assert (startPtr);
        assert (endPtr);
        return endPtr - startPtr;
    }
};

class BlockVersion : public CodeFragment
{
public:

    /// Associated function
    Object fun;

    /// Associated block
    Object block;

    /// Size of the temp stack at the beginning of this version
    uint16_t numTmps;

    /// Code generation context at block entry
    //CodeGenCtx ctx;

    BlockVersion(Object fun, Object block, uint16_t numTmps)
    : fun(fun),
      block(block),
      numTmps(numTmps)
    {
    }
};

/// Struct to associate information with a return address
struct RetEntry
{
    /// Associated call instruction
    refptr callInstr;

    /// Return block version
    BlockVersion* retVer;

    /// Exception/catch block version (may be null)
    BlockVersion* excVer = nullptr;

    /// Temporary stack size before the call instruction
    uint16_t numTmps;
};

/// Information stored by call instructions
struct CallInfo
{
    // Block version to return to after the call
    BlockVersion* retVer;

    // Last seen (cached) function
    refptr lastFn = nullptr;

    // Entry version for the cached function
    BlockVersion* entryVer = nullptr;

    // Number of locals for the cached function
    uint16_t numLocals = 0;

    // Number of call site arguments
    uint16_t numArgs;
};

typedef std::vector<BlockVersion*> VersionList;

/// Initial code heap size in bytes
const size_t CODE_HEAP_INIT_SIZE = 1 << 20;

/// Initial stack size in words
const size_t STACK_INIT_SIZE = 1 << 16;

/// Flat array of bytes into which code gets compiled
uint8_t* codeHeap = nullptr;

/// Limit pointer for the code heap
uint8_t* codeHeapLimit = nullptr;

/// Current allocation pointer in the code heap
uint8_t* codeHeapAlloc = nullptr;

/// Map of block objects to lists of versions
std::unordered_map<refptr, VersionList> versionMap;

/// Map of instructions to block versions
/// Note: this isn't defined for all instructions
std::unordered_map<uint8_t*, BlockVersion*> instrMap;

/// Map of return addresses to associated info
std::unordered_map<BlockVersion*, RetEntry> retAddrMap;

/// Lower stack limit (stack pointer must be greater than this)
Value* stackLimit = nullptr;

/// Stack base, initial stack pointer value (end of the stack memory array)
Value* stackBase = nullptr;

/// Stack frame base pointer
Value* framePtr = nullptr;

/// Current temp stack top pointer
Value* stackPtr = nullptr;

// Current instruction pointer
uint8_t* instrPtr = nullptr;

/// Cache of all possible one-character string values
Value charStrings[256];

void markStackValues() 
{
    for(auto ptr = stackPtr; ptr < stackBase; ptr++)
    {
        Tag tag = ptr->getTag();
        if (tag == TAG_ARRAY || tag == TAG_OBJECT || tag == TAG_STRING)
            vm.markValues(*ptr);
    }
}

/// Write a value to the code heap
template <typename T> void writeCode(T val)
{
    assert (codeHeapAlloc < codeHeapLimit);
    T* heapPtr = (T*)codeHeapAlloc;
    *heapPtr = val;
    codeHeapAlloc += sizeof(T);
    assert (codeHeapAlloc <= codeHeapLimit);
}

/// Deny writing Value objects in the code heap,
/// because they are managed by the garbage collector
void writeCode(Value val)
{
    assert (false && "don't write Value objects into the code heap");
}

/// Return a pointer to a value to read from the code stream
template <typename T> __attribute__((always_inline)) inline T& readCode()
{
    assert (instrPtr + sizeof(T) <= codeHeapLimit);
    T* valPtr = (T*)instrPtr;
    instrPtr += sizeof(T);
    return *valPtr;
}

/// Push a value on the stack
__attribute__((always_inline)) inline void pushVal(Value val)
{
    assert (stackPtr > stackLimit && "stack overflow");
    stackPtr--;
    stackPtr[0] = val;
}

/// Push a boolean on the stack
__attribute__((always_inline)) inline void pushBool(bool val)
{
    pushVal(val? (Value::TRUE) : (Value::FALSE));
}

__attribute__((always_inline)) inline Value popVal()
{
    assert (stackPtr < stackBase && "stack underflow");
    auto val = stackPtr[0];
    stackPtr++;
    return val;
}

__attribute__((always_inline)) inline bool popBool()
{
    // TODO: throw RunError if wrong type
    auto val = popVal();
    assert (val.isBool());
    return (bool)val;
}

__attribute__((always_inline)) inline int32_t popInt32()
{
    // TODO: throw RunError if wrong type
    auto val = popVal();
    assert (val.isInt32());
    return (int32_t)val;
}

__attribute__((always_inline)) inline float popFloat32()
{
    // TODO: throw RunError if wrong type
    auto val = popVal();
    assert (val.isFloat32());
    return (float)val;
}

__attribute__((always_inline)) inline String popStr()
{
    // TODO: throw RunError if wrong type
    auto val = popVal();
    assert (val.isString());
    return (String)val;
}

__attribute__((always_inline)) inline Object popObj()
{
    // TODO: throw RunError if wrong type
    auto val = popVal();
    assert (val.isObject());
    return (Object)val;
}

size_t codeHeapSize()
{
    return codeHeapAlloc - codeHeap;
}

/// Compute the stack size (number of slots allocated)
__attribute__((always_inline)) inline size_t stackSize()
{
    return stackBase - stackPtr;
}

/// Compute the size of the current frame (number of slots allocated)
__attribute__((always_inline)) inline size_t frameSize()
{
    return framePtr - stackPtr + 1;
}

/// Initialize the interpreter
void initInterp()
{
    // Allocate the code heap
    codeHeap = new uint8_t[CODE_HEAP_INIT_SIZE];
    codeHeapLimit = codeHeap + CODE_HEAP_INIT_SIZE;
    codeHeapAlloc = codeHeap;

    // Allocate the stack
    stackLimit = new Value[STACK_INIT_SIZE];
    stackBase = stackLimit + STACK_INIT_SIZE;
    stackPtr = stackBase;
}

/// Get a version of a block. This version will be a stub
/// until compiled
BlockVersion* getBlockVersion(
    Object fun,
    Object block,
    uint16_t numTmps,
    bool forceNew = false
)
{
    auto blockPtr = (refptr)block;
    auto versionItr = versionMap.find((refptr)block);

    // If there are no existing versions of this block
    if (versionItr == versionMap.end())
    {
        versionMap[blockPtr] = VersionList();
    }
    else if (!forceNew)
    {
        auto versions = versionItr->second;
        assert (versions.size() > 0);

        // For each version of this block
        for (auto version : versions)
        {
            if (version->fun != fun)
            {
                continue;
            }

            if (version->numTmps != numTmps)
            {
                throw RunError(
                    "a basic block must always receive the same number of "
                    "values on the temporary stack at its entry"
                );
            }

            return version;
        }
    }

    // Create a new version and add it to the list
    auto& versionList = versionMap[blockPtr];
    auto newVersion = new BlockVersion(fun, block, numTmps);
    versionList.push_back(newVersion);

    return newVersion;
}

void genCall(
    BlockVersion* version,
    Object callInstr,
    size_t numArgs,
    uint16_t& numTmps
)
{
    // Store a mapping of this instruction to the block version
    instrMap[codeHeapAlloc] = version;

    // Arguments and the function object are popped off the stack,
    // a return value or exception is pushed on the stack
    numTmps -= numArgs;

    // Create a return address entry unique to this call instruction
    // and this block version
    RetEntry retEntry;
    retEntry.callInstr = (refptr)callInstr;

    // Store the number of temporaries when the call is performed
    // Note: this excludes the arguments and the function object
    assert (numTmps >= 1);
    retEntry.numTmps = numTmps - 1;

    // Get a version for the call continuation block
    // Note: we force the creation of a new version unique to this call site
    static ICache retToCache("ret_to");
    auto retToBB = retToCache.getObj(callInstr);
    auto retVer = getBlockVersion(version->fun, retToBB, numTmps, true);
    retEntry.retVer = retVer;

    if (callInstr.hasField("throw_to"))
    {
        // Get a version for the exception catch block
        // Note: the catch block expects only one temporary as input
        static ICache throwIC("throw_to");
        auto throwBB = throwIC.getObj(callInstr);
        auto throwVer = getBlockVersion(version->fun, throwBB, 1);
        retEntry.excVer = throwVer;
    }

    // Create an entry for the return address
    retAddrMap[retVer] = retEntry;

    writeCode(CALL);

    CallInfo callInfo;
    callInfo.numArgs = numArgs;
    callInfo.retVer = retVer;
    writeCode(callInfo);
}

std::string getOp(Array& instrs, size_t i)
{
    if (i >= instrs.length())
    {
        //We reached the end of the bb
        return "nop";
    }
    auto instr = (Object)instrs.getElem(i);
    static ICache opIC("op");
    return (std::string)opIC.getStr(instr);
};

void compile(BlockVersion* version)
{
    //std::cout << "compiling version" << std::endl;

    auto block = version->block;

    // Get the instructions array
    static ICache instrsIC("instrs");
    Array instrs = instrsIC.getArr(block);

    if (instrs.length() == 0)
    {
        throw RunError("empty basic block");
    }

    // Mark the block start
    version->startPtr = codeHeapAlloc;

    // Get the size of the temp stack at the beginning of this version
    uint16_t numTmps = version->numTmps;

    // For each instruction
    for (size_t i = 0; i < instrs.length(); ++i)
    {
        auto instrVal = instrs.getElem(i);
        assert (instrVal.isObject());
        auto instr = (Object)instrVal;

        static ICache opIC("op");
        auto op = (std::string)opIC.getStr(instr);

        //std::cout << "op: " << op << std::endl;
        //std::cout << "  numTmps=" << numTmps << std::endl;

        if (op == "push")
        {
            static ICache valIC("val");
            auto val = valIC.getField(instr);
            std::string nextOp = getOp(instrs, i + 1);

            if (nextOp == "add_i32" && val == Value::ONE)
            {
                i += 1;
                writeCode(INC_I32);
                continue;
            }

            if (nextOp == "sub_i32" && val == Value::ONE)
            {
                i += 1;
                writeCode(DEC_I32);
                continue;
            }

            if (nextOp == "get_field")
            {
                i += 1;
                writeCode(GET_FIELD_IMM);
                writeCode((refptr)val);
                writeCode(size_t(0));
                continue;
            }

            numTmps += 1;
            writeCode(PUSH);
            writeCode(val.getWord());
            writeCode(val.getTag());
            continue;
        }

        if (op == "pop")
        {
            numTmps -= 1;
            writeCode(POP);
            continue;
        }

        if (op == "dup")
        {
            numTmps += 1;
            static ICache idxIC("idx");
            auto idx = (uint16_t)idxIC.getInt32(instr);
            writeCode(DUP);
            writeCode(idx);
            continue;
        }

        if (op == "swap")
        {
            writeCode(SWAP);
            continue;
        }

        if (op == "get_local")
        {
            static ICache idxIC("idx");
            auto idx = (uint16_t)idxIC.getInt32(instr);
            if (getOp(instrs, i + 1) == "has_tag")
            {
                numTmps += 1;
                auto nextInstr = (Object) instrs.getElem(i + 1);
                static ICache tagIC("tag");
                auto tagStr = (std::string)tagIC.getStr(nextInstr);
                auto tag = strToTag(tagStr);
                writeCode(LOCAL_HAS_TAG);
                writeCode(idx);
                writeCode(tag);
                i += 1;
                continue;
            }

            numTmps += 1;
            writeCode(GET_LOCAL);
            writeCode(idx);
            continue;
        }

        if (op == "set_local")
        {
            numTmps -= 1;
            static ICache idxIC("idx");
            auto idx = (uint16_t)idxIC.getInt32(instr);
            writeCode(SET_LOCAL);
            writeCode(idx);
            continue;
        }

        //
        // Integer operations
        //

        if (op == "add_i32")
        {
            numTmps -= 1;
            writeCode(ADD_I32);
            continue;
        }

        if (op == "sub_i32")
        {
            numTmps -= 1;
            writeCode(SUB_I32);
            continue;
        }

        if (op == "mul_i32")
        {
            numTmps -= 1;
            writeCode(MUL_I32);
            continue;
        }

        if (op == "div_i32")
        {
            numTmps -= 1;
            writeCode(DIV_I32);
            continue;
        }

        if (op == "mod_i32")
        {
            numTmps -= 1;
            writeCode(MOD_I32);
            continue;
        }

        if (op == "shl_i32")
        {
            numTmps -= 1;
            writeCode(SHL_I32);
            continue;
        }

        if (op == "shr_i32")
        {
            numTmps -= 1;
            writeCode(SHR_I32);
            continue;
        }

        if (op == "ushr_i32")
        {
            numTmps -= 1;
            writeCode(USHR_I32);
            continue;
        }

        if (op == "and_i32")
        {
            numTmps -= 1;
            writeCode(AND_I32);
            continue;
        }

        if (op == "or_i32")
        {
            numTmps -= 1;
            writeCode(OR_I32);
            continue;
        }

        if (op == "xor_i32")
        {
            numTmps -= 1;
            writeCode(XOR_I32);
            continue;
        }

        if (op == "not_i32")
        {
            writeCode(NOT_I32);
            continue;
        }

        if (op == "lt_i32")
        {
            numTmps -= 1;
            writeCode(LT_I32);
            continue;
        }

        if (op == "le_i32")
        {
            numTmps -= 1;
            writeCode(LE_I32);
            continue;
        }

        if (op == "gt_i32")
        {
            numTmps -= 1;
            writeCode(GT_I32);
            continue;
        }

        if (op == "ge_i32")
        {
            numTmps -= 1;
            writeCode(GE_I32);
            continue;
        }

        if (op == "eq_i32")
        {
            numTmps -= 1;
            writeCode(EQ_I32);
            continue;
        }

        //
        // Floating-point ops
        //

        if (op == "add_f32")
        {
            numTmps -= 1;
            writeCode(ADD_F32);
            continue;
        }

        if (op == "sub_f32")
        {
            numTmps -= 1;
            writeCode(SUB_F32);
            continue;
        }

        if (op == "mul_f32")
        {
            numTmps -= 1;
            writeCode(MUL_F32);
            continue;
        }

        if (op == "div_f32")
        {
            numTmps -= 1;
            writeCode(DIV_F32);
            continue;
        }

        if (op == "lt_f32")
        {
            numTmps -= 1;
            writeCode(LT_F32);
            continue;
        }

        if (op == "le_f32")
        {
            numTmps -= 1;
            writeCode(LE_F32);
            continue;
        }

        if (op == "gt_f32")
        {
            numTmps -= 1;
            writeCode(GT_F32);
            continue;
        }

        if (op == "ge_f32")
        {
            numTmps -= 1;
            writeCode(GE_F32);
            continue;
        }

        if (op == "eq_f32")
        {
            numTmps -= 1;
            writeCode(EQ_F32);
            continue;
        }

        if (op == "sin_f32")
        {
            numTmps += 0;
            writeCode(SIN_F32);
            continue;
        }

        if (op == "cos_f32")
        {
            numTmps += 0;
            writeCode(COS_F32);
            continue;
        }

        if (op == "sqrt_f32")
        {
            numTmps += 0;
            writeCode(SQRT_F32);
            continue;
        }

        if (op == "log_f32")
        {
            numTmps += 0;
            writeCode(LOG_F32);
            continue;
        }

        if (op == "exp_f32")
        {
            numTmps += 0;
            writeCode(EXP_F32);
            continue;
        }

        //
        // Conversion ops
        //

        if (op == "i32_to_f32")
        {
            writeCode(I32_TO_F32);
            continue;
        }

        if (op == "i32_to_str")
        {
            writeCode(I32_TO_STR);
            continue;
        }

        if (op == "f32_to_i32")
        {
            writeCode(F32_TO_I32);
            continue;
        }

        if (op == "f32_to_str")
        {
            writeCode(F32_TO_STR);
            continue;
        }

        if (op == "str_to_f32")
        {
            writeCode(STR_TO_F32);
            continue;
        }

        //
        // Miscellaneous ops
        //

        if (op == "eq_bool")
        {
            numTmps -= 1;
            writeCode(EQ_BOOL);
            continue;
        }

        if (op == "has_tag")
        {
            numTmps += 0;
            static ICache tagIC("tag");
            auto tagStr = (std::string)tagIC.getStr(instr);
            auto tag = strToTag(tagStr);

            writeCode(HAS_TAG);
            writeCode(tag);
            continue;
        }

        if (op == "get_tag")
        {
            numTmps += 0;
            writeCode(GET_TAG);
            continue;
        }

        //
        // String operations
        //

        if (op == "str_len")
        {
            numTmps += 0;
            writeCode(STR_LEN);
            continue;
        }

        if (op == "get_char")
        {
            numTmps -= 1;
            writeCode(GET_CHAR);
            continue;
        }

        if (op == "get_char_code")
        {
            numTmps -= 1;
            writeCode(GET_CHAR_CODE);
            continue;
        }

        if (op == "char_to_str")
        {
            numTmps += 0;
            writeCode(CHAR_TO_STR);
            continue;
        }

        if (op == "str_cat")
        {
            numTmps -= 1;
            writeCode(STR_CAT);
            continue;
        }

        if (op == "eq_str")
        {
            numTmps -= 1;
            writeCode(EQ_STR);
            continue;
        }

        //
        // Object operations
        //

        if (op == "new_object")
        {
            numTmps += 0;
            writeCode(NEW_OBJECT);
            continue;
        }

        if (op == "has_field")
        {
            numTmps -= 1;
            writeCode(HAS_FIELD);
            continue;
        }

        if (op == "set_field")
        {
            numTmps -= 3;
            writeCode(SET_FIELD);
            continue;
        }

        if (op == "get_field")
        {
            numTmps -= 1;

            writeCode(GET_FIELD);

            // Cached property slot index
            writeCode(size_t(0));

            continue;
        }

        if (op == "get_field_list")
        {
            numTmps += 0;

            writeCode(GET_FIELD_LIST);
            continue;
        }

        if (op == "eq_obj")
        {
            numTmps -= 1;
            writeCode(EQ_OBJ);
            continue;
        }

        //
        // Array operations
        //

        if (op == "new_array")
        {
            numTmps += 0;
            writeCode(NEW_ARRAY);
            continue;
        }

        if (op == "array_len")
        {
            numTmps += 0;
            writeCode(ARRAY_LEN);
            continue;
        }

        if (op == "array_push")
        {
            numTmps -= 2;
            writeCode(ARRAY_PUSH);
            continue;
        }

        if (op == "array_pop")
        {
            numTmps += 0;
            writeCode(ARRAY_POP);
            continue;
        }

        if (op == "set_elem")
        {
            numTmps -= 3;
            writeCode(SET_ELEM);
            continue;
        }

        if (op == "get_elem")
        {
            numTmps -= 1;
            writeCode(GET_ELEM);
            continue;
        }

        if (op == "eq_array")
        {
            numTmps -= 1;
            writeCode(EQ_ARRAY);
            continue;
        }

        //
        // Branch instructions
        //

        if (op == "jump")
        {
            numTmps += 0;

            static ICache toIC("to");
            auto dstBB = toIC.getObj(instr);
            auto dstVer = getBlockVersion(version->fun, dstBB, numTmps);

            writeCode(JUMP_STUB);
            writeCode(dstVer);
            continue;
        }

        if (op == "if_true")
        {
            numTmps -= 1;

            static ICache thenIC("then");
            static ICache elseIC("else");
            auto thenBB = thenIC.getObj(instr);
            auto elseBB = elseIC.getObj(instr);
            auto thenVer = getBlockVersion(version->fun, thenBB, numTmps);
            auto elseVer = getBlockVersion(version->fun, elseBB, numTmps);

            writeCode(IF_TRUE);
            writeCode(thenVer);
            writeCode(elseVer);

            continue;
        }

        if (op == "call")
        {
            static ICache numArgsCache("num_args");
            auto numArgs = (int16_t)numArgsCache.getInt32(instr);

            genCall(
                version,
                instr,
                numArgs,
                numTmps
            );

            continue;
        }

        if (op == "ret")
        {
            numTmps -= 1;

            // TODO: should report source position (src_pos)
            // of function if this check fails
            if (numTmps != 0)
            {
                throw RunError(
                    "there must be no values left on the temporary stack "
                    "when returning from a function"
                );
            }

            writeCode(RET);
            continue;
        }

        if (op == "throw")
        {
            numTmps -= 1;

            // Store a mapping of this instruction to the block version
            // Needed to retrieve the identity of the current function
            instrMap[codeHeapAlloc] = version;

            writeCode(THROW);
            continue;
        }

        if (op == "import")
        {
            // Push the import function on the stack
            numTmps += 1;
            writeCode(PUSH);
            writeCode((Word)(refptr)&importFn);
            writeCode((Tag)TAG_HOSTFN);

            // Call the import function
            genCall(
                version,
                instr,
                1,
                numTmps
            );

            continue;
        }

        throw RunError("unhandled opcode in basic block \"" + op + "\"");
    }

    // Mark the block end
    version->endPtr = codeHeapAlloc;

    //std::cout << "done compiling version" << std::endl;
    //std::cout << codeHeapSize() << std::endl;
}

/// Get the source position for a given instruction, if available
Value getSrcPos(uint8_t* instrPtr)
{
    auto itr = instrMap.find(instrPtr);
    if (itr == instrMap.end())
    {
        std::cout << "no instr to block mapping" << std::endl;
        return Value::UNDEF;
    }

    auto block = itr->second->block;

    static ICache instrsIC("instrs");
    Array instrs = instrsIC.getArr(block);
    assert (instrs.length() > 0);

    // Traverse the instructions in reverse
    for (int i = (int)instrs.length() - 1; i >= 0; --i)
    {
        auto instrVal = instrs.getElem(i);
        assert (instrVal.isObject());
        auto instr = Object(instrVal);

        if (instr.hasField("src_pos"))
            return instr.getField("src_pos");
    }

    return Value::UNDEF;
}

/// Implementation of the throw instruction
void throwExc(
    uint8_t* throwInstr,
    Value excVal
)
{
    //std::cout << "Entering throwExc" << std::endl;

    // Get the current function
    auto itr = instrMap.find(throwInstr);
    assert (itr != instrMap.end());
    auto curFun = itr->second->fun;

    // Until we are done unwinding the stack
    for (;;)
    {
        //std::cout << "Unwinding frame" << std::endl;

        // Get the number of locals in the function
        static ICache numLocalsIC("num_locals");
        auto numLocals = numLocalsIC.getInt32(curFun);

        //std::cout << "numLocals=" << numLocals << std::endl;

        // Get the saved stack ptr, frame ptr and return address
        auto prevStackPtr = framePtr[-(numLocals + 0)];
        auto prevFramePtr = framePtr[-(numLocals + 1)];
        auto retAddr      = framePtr[-(numLocals + 2)];

        assert (retAddr.getTag() == TAG_RAWPTR);
        auto retVer = (BlockVersion*)retAddr.getWord().ptr;

        // Update the stack and frame pointer
        stackPtr = (Value*)prevStackPtr.getWord().ptr;
        framePtr = (Value*)prevFramePtr.getWord().ptr;

        // If we are at the top level
        if (retVer == nullptr)
        {
            //std::cout << "Uncaught exception" << std::endl;

            std::string errMsg;

            if (excVal.isObject())
            {
                auto excObj = Object(excVal);

                if (excObj.hasField("src_pos"))
                {
                    auto srcPosVal = excObj.getField("src_pos");
                    errMsg += posToString(srcPosVal) + " - ";
                }

                if (excObj.hasField("msg"))
                {
                    auto errMsgVal = excObj.getField("msg");
                    errMsg += errMsgVal.toString();
                }
                else
                {
                    errMsg += "uncaught user exception object";
                }
            }
            else
            {
                errMsg = excVal.toString();
            }

            throw RunError(errMsg);
        }

        // Find the info associated with the return address
        assert (retAddrMap.find(retVer) != retAddrMap.end());
        auto retEntry = retAddrMap[retVer];

        // Get the function associated with the return address
        curFun = retEntry.retVer->fun;

        // If there is an exception handler
        if (retEntry.excVer)
        {
            //std::cout << "Found exception handler" << std::endl;
            //std::cout << "numTmps=" << retEntry.numTmps << std::endl;
            //std::cout << "frameSize()=" << frameSize() << std::endl;

            // Clear the temporary stack
            stackPtr += retEntry.numTmps;

            // Push the exception value on the stack
            pushVal(excVal);

            // Compile exception handler if needed
            if (!retEntry.excVer->startPtr)
                compile(retEntry.excVer);

            instrPtr = retEntry.excVer->startPtr;

            // Done unwinding the stack
            break;
        }
    }
}

void checkArgCount(
    uint8_t* instrPtr,
    size_t numParams,
    size_t numArgs
)
{
    if (numArgs != numParams)
    {
        Value srcPos = getSrcPos(instrPtr);

        std::string srcPosStr = (
            srcPos.isObject()?
            (posToString(srcPos) + " - "):
            std::string("")
        );

        throw RunError(
            srcPosStr +
            "incorrect argument count in call, received " +
            std::to_string(numArgs) +
            ", expected " +
            std::to_string(numParams)
        );
    }
}

/**
Perform a user function call (call to user-implemented Zeta function)
*/
__attribute__((always_inline)) inline void userCall(
    uint8_t* callInstr,
    Object fun,
    CallInfo& callInfo
)
{
    size_t numArgs = callInfo.numArgs;

    // If the function does not match the inline cache
    if (callInfo.lastFn != (refptr)fun)
    {
        // Get a version for the function entry block
        static ICache entryIC("entry");
        auto entryBB = entryIC.getObj(fun);
        auto entryVer = getBlockVersion(fun, entryBB, 0);

        if (!entryVer->startPtr)
        {
            //std::cout << "compiling function entry block" << std::endl;
            compile(entryVer);
        }

        static ICache localsIC("num_locals");
        auto nlocals = localsIC.getInt32(fun);
        assert(nlocals >= 0);
        auto numLocals = size_t(nlocals);

        static ICache paramsIC("params");
        auto params = paramsIC.getArr(fun);
        auto numParams = size_t(params.length());

        // Check that the argument count matches
        checkArgCount(callInstr, numParams, numArgs);

        // Note: the hidden function/closure parameter is always present
        if (numLocals < numParams + 1)
        {
            throw RunError(
                "not enough locals to store function parameters"
            );
        }

        // Update the inline cache
        callInfo.lastFn = (refptr)fun;
        callInfo.numLocals = numLocals;
        callInfo.entryVer = entryVer;
    }

    size_t numLocals = callInfo.numLocals;
    BlockVersion* entryVer = callInfo.entryVer;
    BlockVersion* retVer = callInfo.retVer;

    // Compute the stack pointer to restore after the call
    auto prevStackPtr = stackPtr + numArgs;

    // Save the current frame pointer
    auto prevFramePtr = framePtr;

    // Point the frame pointer to the first argument
    assert (stackPtr > stackLimit);
    framePtr = stackPtr + numArgs - 1;

    // Store the function/pointer argument
    framePtr[-numArgs] = fun;

    // Pop the arguments, push the callee locals
    stackPtr -= numLocals - numArgs;

    pushVal(Value((refptr)prevStackPtr, TAG_RAWPTR));
    pushVal(Value((refptr)prevFramePtr, TAG_RAWPTR));
    pushVal(Value((refptr)retVer, TAG_RAWPTR));

    // Jump to the entry block of the function
    instrPtr = entryVer->startPtr;
}

/**
Perform a host function call (call to internal Zeta function)
*/
__attribute__((always_inline)) inline void hostCall(
    uint8_t* callInstr,
    Value fun,
    size_t numArgs,
    BlockVersion* retVer
)
{
    auto hostFn = (HostFn*)fun.getWord().ptr;

    // Check that the argument count matches
    checkArgCount(callInstr, hostFn->getNumParams(), numArgs);

    // Pointer to the first argument
    auto args = stackPtr + numArgs - 1;

    Value retVal;

    try
    {
        // Call the host function
        switch (numArgs)
        {
            case 0:
            retVal = hostFn->call0();
            break;

            case 1:
            retVal = hostFn->call1(args[0]);
            break;

            case 2:
            retVal = hostFn->call2(args[0], args[-1]);
            break;

            case 3:
            retVal = hostFn->call3(args[0], args[-1], args[-2]);
            break;

            default:
            assert (false);
        }
    }

    catch (RunError err)
    {
        // Pop the arguments from the stack
        stackPtr += numArgs;

        // Create an exception object
        auto excVal = Object();
        auto errStr = String(err.toString());
        excVal.setField("msg", errStr);

        auto retEntry = retAddrMap[retVer];

        // If there is an exception handler (throw_to field)
        if (retEntry.excVer)
        {
            // Clear the temporary stack
            stackPtr += retEntry.numTmps;

            // Push the exception value on the stack
            pushVal(excVal);

            // Compile exception handler if needed
            if (!retEntry.excVer->startPtr)
                compile(retEntry.excVer);

            instrPtr = retEntry.excVer->startPtr;
        }
        else
        {
            // Unwind the interpreter stack
            throwExc(callInstr, excVal);
        }

        return;
    }

    // Pop the arguments from the stack
    stackPtr += numArgs;

    // Push the return value
    pushVal(retVal);

    if (!retVer->startPtr)
        compile(retVer);

    instrPtr = retVer->startPtr;
}

/// Start/continue execution beginning at a current instruction
Value execCode()
{
    assert (instrPtr >= codeHeap);
    assert (instrPtr < codeHeapLimit);

    // For each instruction to execute
    for (;;)
    {
        auto& op = readCode<Opcode>();

        //std::cout << "instr" << std::endl;
        //std::cout << "op=" << (int)op << std::endl;
        //std::cout << "  stack space: " << (stackBase - stackPtr) << std::endl;

        switch (op)
        {
            case PUSH:
            {
                auto word = readCode<Word>();
                auto tag = readCode<Tag>();
                pushVal(Value(word, tag));
            }
            break;

            case POP:
            {
                popVal();
            }
            break;

            case DUP:
            {
                // Read the index of the value to duplicate
                auto idx = readCode<uint16_t>();
                auto val = stackPtr[idx];
                pushVal(val);
            }
            break;

            // Swap the topmost two stack elements
            case SWAP:
            {
                auto v0 = popVal();
                auto v1 = popVal();
                pushVal(v0);
                pushVal(v1);
            }
            break;

            // Set a local variable
            case SET_LOCAL:
            {
                auto localIdx = readCode<uint16_t>();
                //std::cout << "set localIdx=" << localIdx << std::endl;
                assert (stackPtr > stackLimit);
                framePtr[-localIdx] = popVal();
            }
            break;

            case GET_LOCAL:
            {
                // Read the index of the value to push
                auto localIdx = readCode<uint16_t>();
                //std::cout << "get localIdx=" << localIdx << std::endl;
                assert (stackPtr > stackLimit);
                auto val = framePtr[-localIdx];
                pushVal(val);
            }
            break;

            case LOCAL_HAS_TAG:
            {
                // Read the index of the local value
                auto localIdx = readCode<uint16_t>();
                auto testTag = readCode<Tag>();

                assert (stackPtr > stackLimit);
                auto val = framePtr[-localIdx];

                auto valTag = val.getTag();
                pushBool(valTag == testTag);
            }
            break;

            //
            // Integer operations
            //
            case INC_I32:
            {
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 + 1));
            }
            break;
            case DEC_I32:
            {
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 - 1));
            }
            break;

            case ADD_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 + arg1));
            }
            break;

            case SUB_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 - arg1));
            }
            break;

            case MUL_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 * arg1));
            }
            break;

            case DIV_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 / arg1));
            }
            break;

            case MOD_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 % arg1));
            }
            break;

            case SHL_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 << arg1));
            }
            break;

            case SHR_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 >> arg1));
            }
            break;

            case USHR_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = (uint32_t)popInt32();
                pushVal(Value::int32((int32_t)(arg0 >> arg1)));
            }
            break;

            case AND_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 & arg1));
            }
            break;

            case OR_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 | arg1));
            }
            break;

            case XOR_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushVal(Value::int32(arg0 ^ arg1));
            }
            break;

            case NOT_I32:
            {
                auto arg0 = popInt32();
                pushVal(Value::int32(~arg0));
            }
            break;

            case LT_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushBool(arg0 < arg1);
            }
            break;

            case LE_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushBool(arg0 <= arg1);
            }
            break;

            case GT_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushBool(arg0 > arg1);
            }
            break;

            case GE_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushBool(arg0 >= arg1);
            }
            break;

            case EQ_I32:
            {
                auto arg1 = popInt32();
                auto arg0 = popInt32();
                pushBool(arg0 == arg1);
            }
            break;

            //
            // Floating-point operations
            //

            case ADD_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushVal(Value::float32(arg0 + arg1));
            }
            break;

            case SUB_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushVal(Value::float32(arg0 - arg1));
            }
            break;

            case MUL_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushVal(Value::float32(arg0 * arg1));
            }
            break;

            case DIV_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushVal(Value::float32(arg0 / arg1));
            }
            break;

            case LT_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushBool(arg0 < arg1);
            }
            break;

            case LE_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushBool(arg0 <= arg1);
            }
            break;

            case GT_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushBool(arg0 > arg1);
            }
            break;

            case GE_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushBool(arg0 >= arg1);
            }
            break;

            case EQ_F32:
            {
                auto arg1 = popFloat32();
                auto arg0 = popFloat32();
                pushBool(arg0 == arg1);
            }
            break;

            case SIN_F32:
            {
                float arg = popFloat32();
                pushVal(Value::float32(sin(arg)));
            }
            break;

            case COS_F32:
            {
                float arg = popFloat32();
                pushVal(Value::float32(cos(arg)));
            }
            break;

            case SQRT_F32:
            {
                float arg = popFloat32();
                pushVal(Value::float32(sqrt(arg)));
            }
            break;

            case LOG_F32:
            {
                float arg = popFloat32();

                if (arg <= 0)
                    throw RunError("log input must be strictly positive");

                pushVal(Value::float32(log(arg)));
            }
            break;

            case EXP_F32:
            {
                float arg = popFloat32();
                auto r = exp(arg);
                pushVal(Value::float32(r));
            }
            break;

            //
            // Conversion operations
            //

            case I32_TO_F32:
            {
                auto arg0 = popInt32();
                pushVal(Value::float32(arg0));
            }
            break;

            case I32_TO_STR:
            {
                auto arg0 = popInt32();
                String str = std::to_string(arg0);
                pushVal(str);
            }
            break;

            case F32_TO_I32:
            {
                auto arg0 = popFloat32();
                pushVal(Value::int32(arg0));
            }
            break;

            case F32_TO_STR:
            {
                auto arg0 = popFloat32();
                String str = std::to_string(arg0);
                pushVal(str);
            }
            break;

            case STR_TO_F32:
            {
                auto arg0 = popStr();

                float val;

                // If the float fails to parse, produce NaN
                try
                {
                    val = std::stof(arg0);
                }
                catch (...)
                {
                    val = 0.0f / 0.0f;
                }

                pushVal(Value::float32(val));
            }
            break;

            //
            // Misc operations
            //

            case EQ_BOOL:
            {
                auto arg1 = popBool();
                auto arg0 = popBool();
                pushBool(arg0 == arg1);
            }
            break;

            // Test if a value has a given tag
            case HAS_TAG:
            {
                auto testTag = readCode<Tag>();
                auto valTag = popVal().getTag();
                pushBool(valTag == testTag);
            }
            break;

            // Get the type tag associated with a value.
            // Note: this produces a string
            case GET_TAG:
            {
                auto valTag = popVal().getTag();
                auto tagStr = tagToStr(valTag);
                pushVal(String(tagStr));
            }
            break;

            //
            // String operations
            //

            case STR_LEN:
            {
                auto str = popStr();
                pushVal(Value::int32(str.length()));
            }
            break;

            case GET_CHAR:
            {
                auto idx = (size_t)popInt32();
                auto str = popStr();

                if (idx >= str.length())
                {
                    throw RunError(
                        "get_char, index out of bounds"
                    );
                }

                uint8_t ch = str[idx];
                // Cache single-character strings
                if (charStrings[ch] == Value::UNDEF)
                {
                    char buf[2] = { (char)str[idx], '\0' };
                    charStrings[ch] = String(buf);
                }

                pushVal(charStrings[ch]);
            }
            break;

            case GET_CHAR_CODE:
            {
                auto idx = (size_t)popInt32();
                auto str = popStr();

                if (idx >= str.length())
                {
                    throw RunError(
                        "get_char_code, index out of bounds"
                    );
                }

                unsigned char ch = (unsigned char)str[idx];
                pushVal(Value::int32(ch));
            }
            break;

            case CHAR_TO_STR:
            {
                auto charCode = (char)popInt32();
                char buf[2] = { (char)charCode, '\0' };
                pushVal(String(buf));
            }
            break;

            case STR_CAT:
            {
                auto a = popStr();
                auto b = popStr();
                auto c = String::concat(b, a);
                pushVal(c);
            }
            break;

            case EQ_STR:
            {
                auto arg1 = popStr();
                auto arg0 = popStr();
                pushBool(arg0 == arg1);
            }
            break;

            //
            // Object operations
            //

            case NEW_OBJECT:
            {
                auto capacity = popInt32();
                auto obj = Object(capacity);
                pushVal(obj);
            }
            break;

            case HAS_FIELD:
            {
                auto fieldName = popStr();
                auto obj = popObj();
                pushBool(obj.hasField(fieldName));
            }
            break;

            case SET_FIELD:
            {
                auto val = popVal();
                auto fieldName = popStr();
                auto obj = popObj();
                obj.setField(fieldName, val);
            }
            break;

            // This instruction will abort execution if trying to
            // access a field that is not present on an object.
            // The running program is responsible for testing that
            // fields exist before attempting to read them.
            case GET_FIELD:
            {
                auto fieldName = popStr();
                auto obj = popObj();

                // Get the cached slot index
                auto& slotIdx = readCode<size_t>();

                Value val;

                if (!obj.getField(fieldName, val, slotIdx))
                {
                    throw RunError(
                        "get_field failed, missing field \"" +
                        (std::string)fieldName + "\""
                    );
                }

                pushVal(val);
            }
            break;

            case GET_FIELD_IMM:
            {
                refptr nameStrPtr = readCode<refptr>();
                String fieldName = Value(nameStrPtr, TAG_STRING);

                auto obj = popObj();

                // Get the cached slot index
                auto& slotIdx = readCode<size_t>();

                Value val;

                if (!obj.getField(fieldName, val, slotIdx))
                {
                    throw RunError(
                        "get_field failed, missing field \"" +
                        (std::string)fieldName + "\""
                    );
                }

                pushVal(val);
            }
            break;

            case GET_FIELD_LIST:
            {
                Value arg0 = popVal();
                Array array = Array(0);
                for (auto itr = ObjFieldItr(arg0); itr.valid(); itr.next())
                {
                    auto fieldName = (String)itr.get();
                    array.push(fieldName);
                }
                pushVal(array);
            }
            break;

            case EQ_OBJ:
            {
                Value arg1 = popVal();
                Value arg0 = popVal();
                pushBool(arg0 == arg1);
            }
            break;

            //
            // Array operations
            //

            case NEW_ARRAY:
            {
                // Note: capacity refers to preallocated slots,
                // the new array will have length 0
                auto capacity = popInt32();
                auto array = Array(capacity);
                pushVal(array);
            }
            break;

            case ARRAY_LEN:
            {
                auto arr = Array(popVal());
                pushVal(Value::int32(arr.length()));
            }
            break;

            case ARRAY_PUSH:
            {
                auto val = popVal();
                auto arr = Array(popVal());
                arr.push(val);
            }
            break;

            case ARRAY_POP:
            {
                auto arr = Array(popVal());
                auto val = arr.pop();
                pushVal(val);
            }
            break;

            case SET_ELEM:
            {
                auto val = popVal();
                auto idx = (size_t)popInt32();
                auto arr = Array(popVal());

                if (idx >= arr.length())
                {
                    throw RunError(
                        "set_elem, index out of bounds"
                    );
                }

                arr.setElem(idx, val);
            }
            break;

            case GET_ELEM:
            {
                auto idx = (size_t)popInt32();
                auto arr = Array(popVal());

                if (idx >= arr.length())
                {
                    throw RunError(
                        "get_elem, index out of bounds"
                    );
                }

                pushVal(arr.getElem(idx));
            }
            break;

            case EQ_ARRAY:
            {
                Value arg1 = popVal();
                Value arg0 = popVal();
                pushBool(arg0 == arg1);
            }
            break;

            //
            // Branch instructions
            //

            case JUMP_STUB:
            {
                auto& dstAddr = readCode<uint8_t*>();

                //std::cout << "Patching jump" << std::endl;

                auto dstVer = (BlockVersion*)dstAddr;

                if (!dstVer->startPtr)
                {
                    // If the heap allocation pointer is right
                    // after the jump instruction
                    if (instrPtr == codeHeapAlloc)
                    {
                        // The jump is redundant, so we will write the
                        // next block over this jump instruction
                        instrPtr = codeHeapAlloc = (uint8_t*)&op;
                    }

                    compile(dstVer);
                }
                else
                {
                    // Patch the jump
                    op = JUMP;
                    dstAddr = dstVer->startPtr;

                    // Jump to the target
                    instrPtr = dstVer->startPtr;
                }
            }
            break;

            case JUMP:
            {
                auto& dstAddr = readCode<uint8_t*>();
                instrPtr = dstAddr;
            }
            break;

            case IF_TRUE:
            {
                auto& thenAddr = readCode<uint8_t*>();
                auto& elseAddr = readCode<uint8_t*>();

                auto arg0 = popVal();

                if (arg0 == Value::TRUE)
                {
                    if (thenAddr < codeHeap || thenAddr >= codeHeapLimit)
                    {
                        //std::cout << "Patching then target" << std::endl;

                        auto thenVer = (BlockVersion*)thenAddr;
                        if (!thenVer->startPtr)
                           compile(thenVer);

                        // Patch the jump
                        thenAddr = thenVer->startPtr;
                    }

                    instrPtr = thenAddr;
                }
                else
                {
                    if (elseAddr < codeHeap || elseAddr >= codeHeapLimit)
                    {
                       //std::cout << "Patching else target" << std::endl;

                       auto elseVer = (BlockVersion*)elseAddr;
                       if (!elseVer->startPtr)
                           compile(elseVer);

                       // Patch the jump
                       elseAddr = elseVer->startPtr;
                    }

                    instrPtr = elseAddr;
                }
            }
            break;

            // Regular function call
            case CALL:
            {
                auto& callInfo = readCode<CallInfo>();

                auto callee = popVal();

                if (stackSize() < callInfo.numArgs)
                {
                    throw RunError(
                        "stack underflow at call"
                    );
                }

                if (callee.isObject())
                {
                    userCall(
                        (uint8_t*)&op,
                        callee,
                        callInfo
                    );
                }
                else if (callee.isHostFn())
                {
                    hostCall(
                        (uint8_t*)&op,
                        callee,
                        callInfo.numArgs,
                        callInfo.retVer
                    );
                }
                else
                {
                  throw RunError("invalid callee at call site");
                }
            }
            break;

            case RET:
            {
                // TODO: figure out callee identity from version,
                // caller identity from return address
                //
                // We want args to have been consumed
                // We pop all our locals (or the caller does)
                //
                // The thing is... The caller can't pop our locals,
                // because the call continuation doesn't know

                // Pop the return value
                auto retVal = popVal();

                // Pop the return address
                auto retVer = (BlockVersion*)popVal().getWord().ptr;

                // Pop the previous frame pointer
                auto prevFramePtr = popVal().getWord().ptr;

                // Pop the previous stack pointer
                auto prevStackPtr = popVal().getWord().ptr;

                // Restore the previous frame pointer
                framePtr = (Value*)prevFramePtr;

                // Restore the stack pointer
                stackPtr = (Value*)prevStackPtr;

                // If this is a top-level return
                if (retVer == nullptr)
                {
                    return retVal;
                }
                else
                {
                    // Push the return value on the stack
                    pushVal(retVal);

                    if (!retVer->startPtr)
                        compile(retVer);

                    instrPtr = retVer->startPtr;
                }
            }
            break;

            // Throw an exception
            case THROW:
            {
                // Pop the exception value
                auto excVal = popVal();
                throwExc((uint8_t*)&op, excVal);
            }
            break;

            default:
            assert (false && "unhandled instruction in interpreter loop");
        }

    }

    assert (false);
}

/**
Call into a user function from an outside context
Note: this may be indirectly called from within a running interpreter
*/
Value callFun(Object fun, ValueVec args)
{
    auto params = fun.getFieldArr("params");
    auto numParams = size_t(params.length());
    auto nlocals = fun.getFieldInt32("num_locals");
    assert(nlocals >= 0);
    auto numLocals = size_t(nlocals);

    if (args.size() != numParams)
    {
        throw RunError(
            "argument count mismatch in top-level call"
        );
    }

    if (numLocals < numParams + 1)
    {
        throw RunError(
            "not enough locals to store function parameters in top-level call"
        );
    }

    // Store the stack size before the call
    auto preCallSz = stackSize();

    // Store the instruction pointer before the call
    auto prevInstrPtr = instrPtr;

    // Save the previous stack and frame pointers
    auto prevStackPtr = stackPtr;
    auto prevFramePtr = framePtr;

    // Initialize the frame pointer (used to access locals)
    framePtr = stackPtr - 1;

    // Push space for the local variables
    stackPtr -= numLocals;
    assert (stackPtr >= stackLimit);

    // Push the previous stack pointer, previous
    // frame pointer and return address
    pushVal(Value((refptr)prevStackPtr, TAG_RAWPTR));
    pushVal(Value((refptr)prevFramePtr, TAG_RAWPTR));
    pushVal(Value(nullptr, TAG_RAWPTR));

    // Copy the arguments into the locals
    for (size_t i = 0; i < args.size(); ++i)
    {
        //std::cout << "  " << args[i].toString() << std::endl;
        framePtr[-i] = args[i];
    }

    // Store the function/closure parameter
    framePtr[-numParams] = fun;

    // Get the function entry block
    static ICache entryIC("entry");
    auto entryBlock = entryIC.getObj(fun);
    auto entryVer = getBlockVersion(fun, entryBlock, 0);

    // Generate code for the entry block version
    compile(entryVer);
    assert (entryVer->length() > 0);

    // Begin execution at the entry block
    instrPtr = entryVer->startPtr;
    auto retVal = execCode();

    // Restore the previous instruction pointer
    instrPtr = prevInstrPtr;

    // Check that the stack size matches what it was before the call
    if (stackSize() != preCallSz)
    {
        throw RunError("stack size does not match after call termination");
    }

    return retVal;
}

/// Call a function exported by a package
Value callExportFn(
    Object pkg,
    std::string fnName,
    ValueVec args
)
{
    if (!pkg.hasField(fnName))
    {
        throw RunError(
            "package does not export function \"" + fnName + "\""
        );
    }

    auto fnVal = pkg.getField(fnName);

    if (!fnVal.isObject())
    {
        throw RunError(
            "field \"" + fnName + "\" exported by package is not a function"
        );
    }

    auto funObj = Object(fnVal);

    return callFun(funObj, args);
}

Value testRunImage(std::string fileName)
{
    std::cout << "loading image \"" << fileName << "\"" << std::endl;

    auto pkg = parseFile(fileName);

    std::cout << callExportFn(pkg, "main").toString() << "\n";

    return callExportFn(pkg, "main");
}

void testInterp()
{
    assert (testRunImage("tests/vm/ex_ret_cst.zim") == Value::int32(777));
    assert (testRunImage("tests/vm/ex_loop_cnt.zim") == Value::int32(0));
    assert (testRunImage("tests/vm/ex_image.zim") == Value::int32(10));
    assert (testRunImage("tests/vm/ex_rec_fact.zim") == Value::int32(5040));
    assert (testRunImage("tests/vm/ex_fibonacci.zim") == Value::int32(377));
    assert (testRunImage("tests/vm/float_ops.zim").toString() == "10.500000");
}
