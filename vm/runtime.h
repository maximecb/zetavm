#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <cstring>
#include <unordered_map>
#include <iostream>

/// Type tag, 8 bits
typedef uint8_t Tag;

/// Heap pointer type
typedef uint8_t* refptr;

/// Type of object header
typedef uintptr_t obj_header;

/// Type tag constants
const Tag TAG_UNDEF     = 0;
const Tag TAG_BOOL      = 1;
const Tag TAG_INT32     = 2;
const Tag TAG_INT64     = 3;
const Tag TAG_FLOAT32   = 4;
const Tag TAG_FLOAT64   = 5;
const Tag TAG_STRING    = 6;
const Tag TAG_OBJECT    = 7;
const Tag TAG_ARRAY     = 8;
const Tag TAG_HOSTFN    = 9;
const Tag TAG_RAWPTR    = 10;
const Tag TAG_IMGREF    = 11;

/// Object header size
const size_t HEADER_SIZE = sizeof(obj_header);

/// Bit flag indicating the next pointer is set
const size_t HEADER_IDX_NEXT = 15;
const size_t HEADER_MSK_NEXT = 1 << HEADER_IDX_NEXT;

/// Offset of the next pointer
const size_t OBJ_OF_NEXT = HEADER_SIZE;

/**
64-bit word union
*/
union Word
{
    Word(refptr p) { int64 = 0; ptr = p; }
    Word(int64_t v) { int64 = v; }
    Word(float v) { float32 = v; }
    Word() {}

    float float32;
    int64_t int64;
    int32_t int32;
    int8_t int8;
    refptr ptr;
};

/**
Tagged value pair type (64-bit word + tag)
*/
class Value
{
private:

    Word word;
    Tag tag;

public:

    static const Value ZERO;
    static const Value ONE;
    static const Value TWO;

    static const Value UNDEF;
    static const Value TRUE;
    static const Value FALSE;

    Value() : Value(UNDEF.word, UNDEF.tag) {}
    Value(refptr p, Tag t) : Value(Word(p), t) {}
    Value(Word w, Tag t) : word(w), tag(t) {};
    ~Value() {}

    // Static constructors. These are needed because of type ambiguity.
    static Value int32(int32_t v) { return Value(Word((int64_t)v), TAG_INT32); }
    static Value float32(float v) { return Value(Word(v), TAG_FLOAT32); }

    bool isBool() const { return tag == TAG_BOOL; }
    bool isInt32() const { return tag == TAG_INT32; }
    bool isFloat32() const { return tag == TAG_FLOAT32; }
    bool isString() const { return tag == TAG_STRING; }
    bool isObject() const { return tag == TAG_OBJECT; }
    bool isArray() const { return tag == TAG_ARRAY; }
    bool isHostFn() const { return tag == TAG_HOSTFN; }

    Word getWord() const { return word; }
    Tag getTag() const { return tag; }

    bool isPointer() const;

    std::string toString() const;

    inline operator bool () const
    {
        assert (tag == TAG_BOOL);
        return word.int64? 1:0;
    }

    inline operator int32_t () const
    {
        assert (tag == TAG_INT32);
        return word.int32;
    }

    inline operator float () const
    {
        assert (tag == TAG_FLOAT32);
        return word.float32;
    }

    inline operator refptr () const
    {
        assert (isPointer());
        return word.ptr;
    }

    operator std::string () const;

    bool operator == (const Value& that) const
    {
        return this->word.int64 == that.word.int64 && this->tag == that.tag;
    }

    bool operator != (const Value& that) const
    {
        return !(*this == that);
    }
};

/**
Virtual Machine object (singleton)
*/
class VM
{
private:

    // TODO: total memory size allocated

    // TODO: dynamically grow pools?

    // TODO: pools for sizes up to 32 (words)

    // TODO: number of elements allocated in each pool

public:

    VM();

    /// Allocate a block of memory on the heap
    Value alloc(uint32_t size, Tag tag);

    size_t allocated() const;
};


/**
Heap object value wrapper base class
*/
class Wrapper
{
protected:

    /// Internal value holding a heap pointer to the string
    Value val;

    Wrapper() {}

    /// Set the next pointer for an object
    void setNextPtr(refptr obj, refptr nextPtr);

    /// Get the next pointer for an object
    refptr getNextPtr(refptr obj, refptr notFound);

    /// Get a pointer to the object, or its next pointer if set
    /// Note: this method is necessary because objects may be
    ///       extended through indirection.
    refptr getObjPtr();

public:

    uint32_t length() const;

    /// Wrappers must be passed by value, new/delete not allowed
    void* operator new (size_t sz) = delete;
    void operator delete (void* p) = delete;

    /// Casting operator to get the wrapped value
    operator Value () { return val; }

    /// Casting operator to get the wrapped pointer
    operator refptr () { return val; }
};

/**
Wrapper to manipulate string values
Note: strings are UTF-8 and null-terminated
*/
class String : public Wrapper
{
public:

    /// Offset and size of the length and data fields
    static const size_t OF_LEN = HEADER_SIZE;
    static const size_t SZ_LEN = sizeof(uint32_t);
    static const size_t OF_DATA = OF_LEN + SZ_LEN;

    /// Compute the size of an object of this type
    static constexpr size_t memSize(size_t len)
    {
        return OF_DATA + (len + 1) * sizeof(char);
    }

    String(std::string str);
    String(Value value);

    /// Get the length of the string
    uint32_t length() const;

    /// Get the raw internal character data
    /// Warning: this data can get garbage-collected
    const char* getDataPtr() const;

    /// Casting operator to extract a string value
    operator std::string ();

    /// Comparison with a string literal
    bool operator == (const char* that) const;

    // FIXME: temporary until string interning is implemented
    bool operator == (String that) {
        return val == that.val;
    }

    /// Get the ith character code
    char operator [] (size_t i);

    /// Concatenate two strings
    static String concat(String a, String b);
};

/**
Array value wrapper
Note: arrays have a fixed length set at allocation time
*/
class Array : public Wrapper
{
private:

    /// Get the array capacity
    /// Note: we want to avoid publicly exposing the array capacity
    size_t getCap();

public:

    /// Offset and size of the fields
    static const size_t OF_CAP = HEADER_SIZE;
    static const size_t SZ_CAP = sizeof(uint32_t);
    static const size_t OF_LEN = OF_CAP + SZ_CAP;
    static const size_t SZ_LEN = sizeof(uint32_t);
    static const size_t OF_DATA = OF_LEN + SZ_LEN;

    /// Compute the size of an object of this type
    static constexpr size_t memSize(size_t cap)
    {
        return OF_DATA + cap * sizeof(Word) + cap * sizeof(Tag);
    }

    /// Allocate a new array of a given length
    Array(size_t minCap);

    /// Create an array wrapper from a tagged value
    Array(Value value);

    /// Get the length of the array
    uint32_t length();

    /// Set the value of the ith element
    void setElem(size_t i, Value v);

    /// Get the value of the ith element
    Value getElem(size_t i);

    /// Append a value to the array
    void push(Value val);

    /// Pop a value off the end of the array
    Value pop();
};

/**
Object value wrapper
*/
class Object : public Wrapper
{
    friend class ObjFieldItr;

    /// Get the object's capacity
    size_t getCap();

    /// Find the slot index at which a field is stored
    size_t getSlotIdx(
        refptr ptr,
        size_t cap,
        String fieldName,
        bool newField
    );

public:

    /// Minimum guaranteed object capacity
    static const size_t MIN_CAP = 16;

    /// Offset and size of the fields
    static const size_t OF_CAP = HEADER_SIZE;
    static const size_t SZ_CAP = sizeof(uint32_t);
    static const size_t OF_SHAPE = OF_CAP + SZ_CAP;
    static const size_t SZ_SHAPE = sizeof(refptr);
    static const size_t OF_FIELDS = OF_SHAPE + SZ_SHAPE;

    /// Compute the size of an object of this type
    static constexpr size_t memSize(size_t cap)
    {
        // FIXME: for now, we store tagged values, this will change
        // once we have proper shapes implemented
        //return OF_FIELDS + cap * sizeof(Word);
        return OF_FIELDS + cap * sizeof(Value);
    }

    /// Allocate a new empty object
    static Object newObject(size_t cap = 0);

    Object(Value value);

    bool hasField(String name);
    void setField(String name, Value val);
    Value getField(String name);

    /// Property lookup with a slot index cache
    bool getField(const String& name, Value& value, size_t& idxCache);

    bool hasField(std::string name) { return hasField(String(name)); }
    void setField(std::string name, Value val) { return setField(String(name), val); }
    Value getField(std::string name) { return getField(String(name)); }

    // Property lookups with type checking
    int32_t getFieldInt32(std::string name);
    Object getFieldObj(std::string name);
    Array getFieldArr(std::string name);
};

/**
Run-time error exception class
*/
class RunError
{
protected:

    RunError() {}

    Object val = Object::newObject(2);

public:

    RunError(std::string errorMsg)
    {
        val.setField("thrown_value", String(errorMsg));
        val.setField("stack", Array(10));
    }
    
    RunError(Object nested) : val(nested)
    {
    }
    
    RunError(Value thrownValue)
    {
        val.setField("thrown_value", thrownValue);
        val.setField("stack", Array(10));
    }

    virtual ~RunError()
    {
    }

    virtual std::string toString();

    Object getExcVal() const
    {
        return val;
    }
};

/**
Inline cache to speed up property lookups
*/
class ICache
{
private:

    // Cached slot index
    size_t slotIdx = 0;

    // Field name to look up
    String fieldName;

public:

    ICache(std::string fieldName)
    : fieldName(fieldName)
    {
    }

    Value getField(Object obj)
    {
        Value val;

        if (!obj.getField(fieldName, val, slotIdx))
        {
            throw RunError("missing field \"" + (std::string)fieldName + "\"");
        }

        return val;
    }

    int32_t getInt32(Object obj)
    {
        auto val = getField(obj);
        assert (val.isInt32());
        return (int32_t)val;
    }

    String getStr(Object obj)
    {
        auto val = getField(obj);
        assert (val.isString());
        return String(val);
    }

    Object getObj(Object obj)
    {
        auto val = getField(obj);
        assert (val.isObject());
        return Object(val);
    }

    Array getArr(Object obj)
    {
        auto val = getField(obj);
        assert (val.isArray());
        return Array(val);
    }
};

/**
Object field iterator
*/
class ObjFieldItr
{
private:

    Object obj;

    size_t slotIdx = 0;

public:

    ObjFieldItr(Object obj);

    bool valid();

    std::string get();

    void next();
};

/**
Image reference/pointer placeholder
This is used for linkage during image loading, so that
image files can contain circular references.
*/
class ImgRef : public Wrapper
{
public:

    // This object contains only a header and a string pointer
    static const size_t OF_SYM = HEADER_SIZE;
    static const size_t SIZE = OF_SYM + sizeof(refptr);

    ImgRef(String symbol);
    ImgRef(Value val);

    std::string getName() const;
};

int64_t murmurHash2(const void* key, size_t len, uint64_t seed);

class StringPool
{
private:
    class StringHasher
    {
    public:
        size_t operator()(const std::string str) const
        {
            auto len = str.length();
            auto ptr = str.c_str();
            return (size_t)murmurHash2(ptr, len, 1337);
        }
    };
    std::unordered_map<std::string, Value, StringHasher> pool;
    Value newString(std::string str);

public:
    StringPool();
    Value getString(std::string str);
};

/// Global virtual machine instance
extern VM vm;

/// Check if a string is a valid identifier
bool isValidIdent(std::string identStr);

/// Get the tag enumeration value for a given tag string
Tag strToTag(std::string str);

/// Get the string representation for a type tag
std::string tagToStr(Tag tag);

/// Get a string representation of a source position object
std::string posToString(Value srcPos);

/// Unit test for the runtime
void testRuntime();

