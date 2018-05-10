#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "runtime.h"
#include "interp.h"

/// Undefined value constant
/// Note: zeroed memory is automatically undefined
const Value Value::UNDEF(Word(int64_t(0)), TAG_UNDEF);

/// Boolean constants
const Value Value::FALSE(Word(int64_t(0)), TAG_BOOL);
const Value Value::TRUE(Word(int64_t(1)), TAG_BOOL);

/// Numerical constants
const Value Value::ZERO(Word(int64_t(0)), TAG_INT32);
const Value Value::ONE(Word(int64_t(1)), TAG_INT32);
const Value Value::TWO(Word(int64_t(2)), TAG_INT32);

// Global virtual machine instance
VM vm;

// Global string pool
StringPool stringPool;

bool isGCRoot(Tag tag){
    return tag == TAG_OBJECT || tag == TAG_ARRAY || tag == TAG_STRING || tag == TAG_IMGREF;
}

/// Produce a string representation of a value
std::string Value::toString() const
{
    switch (tag)
    {
        case TAG_UNDEF:
        return "$undef";

        case TAG_BOOL:
        return (*this == Value::TRUE)? "$true":"$false";

        case TAG_INT32:
        return std::to_string(word.int32);

        case TAG_FLOAT32:
        return std::to_string(word.float32);

        case TAG_STRING:
        return (std::string)*this;

        case TAG_ARRAY:
        return "array";

        case TAG_OBJECT:
        return "object";

        default:
        assert (false);
    }
}

/// Determine if this value is of a pointer type
bool Value::isPointer() const
{
    switch (tag)
    {
        case TAG_STRING:
        case TAG_ARRAY:
        case TAG_OBJECT:
        case TAG_IMGREF:
        return true;

        default:
        return false;
    }
}

Value::Value(Word w, Tag t) : word(w), tag(t) 
{
    if (isGCRoot(t))
    {
        Value* head = vm.head;
        next = head;        
        if (head != NULL)
        {
            vm.head->prev = this;
        }
        vm.head = this;
    }
};

Value::~Value() {
    if (isGCRoot(tag))
    {
        if (prev != NULL)
        {
            prev->next = next;
        }
        else if (vm.head == this)
        {
            vm.head = next;
        }
        if (next != NULL)
        {   
            next->prev = prev;
        }
        prev = NULL;
        next = NULL;
    }
}

Value::Value(const Value& val)
{
    if (isGCRoot(val.getTag()))
    {
        Value* head = vm.head;
        next = head; 
        prev = NULL;       
        if (head != NULL)
        {
            vm.head->prev = this;
        }
        vm.head = this;
    }
    tag = val.getTag();
    word = val.getWord();
}

Value& Value::operator= (const Value& val)
{
    if (isGCRoot(val.getTag()) && next == NULL && prev == NULL)
    {
        Value* head = vm.head;
        next = head; 
        prev = NULL;       
        if (head != NULL)
        {
            vm.head->prev = this;
        }
        vm.head = this;
    } 
    else if (!isGCRoot(val.getTag()) && isGCRoot(tag))
    {
        if (prev != NULL)
        {
            prev->next = next;
        }
        else if (vm.head == this)
        {
            vm.head = next;
        }
        if (next != NULL)
        {   
            next->prev = prev;
        }
        prev = NULL;
        next = NULL;
    } 
    tag = val.getTag();
    word = val.getWord();
    return *this;
}

bool isMarked(refptr obj)
{ 
    auto header = *(uint64_t*)obj;
    return (header & HEADER_MSK_MARK);
}
void setMark(refptr obj) 
{ 
    auto header = *(uint64_t*)obj;
    *(uint64_t*)(obj) = header | HEADER_MSK_MARK;
}
void clearMark(refptr obj)
{
    auto header = *(uint64_t*)obj;
    *(uint64_t*)(obj) = header & (~HEADER_MSK_MARK);
}

Value::operator std::string () const
{
    assert (isString());
    return String(*this);
}

VM::VM()
{
}

/**
Allocates a block of memory
Note that this function guarantees that the memory is zeroed out
*/


Value VM::alloc(uint32_t size, Tag tag)
{
    // FIXME: use an alloc pool of some kind
    if (allocated >= limit)
    {   
        mark();
        sweep();
        limit = allocated * 2;   
    } 
    auto ptr = (refptr)calloc(1, size);
    if (isGCRoot(tag))
    {
        values.push_back(ptr);
    }
    //Set the tag in the object header
    *(Tag*)ptr = tag;
    allocated += (size_t)size;
    // Wrap the pointer in a tagged value
    return Value(ptr, tag);
}

void VM::mark()
{
    markStackValues();
    Value* val = head;
    while (val != NULL)
    {   
        markValues(*val);
        val = val->next;
    }
}

void VM::markValues(Value root)
{   
    Tag tag = root.getTag();
    if (!(isGCRoot(tag))) return;
    std::vector<refptr> toMark = { (refptr)(root) };
    while (!toMark.empty())
    {
        refptr ptr = toMark.back();
        toMark.pop_back();
        Tag tag = *(Tag*)ptr;
        // std::cout << tagToStr(tag) << std::endl;
        if (isGCRoot(tag))
        {
            // If this node was previously visited, skip it
            if (isMarked(ptr))
                continue;
            // Mark the node as visited
            setMark(ptr);
        }
        if (tag == TAG_OBJECT)
        {
            // std::cout << "obj" << std::endl;
            Object objRoot = (Object)Value(ptr, tag);;
            for (auto itr = ObjFieldItr(objRoot); itr.valid(); itr.next())
            {
                auto fieldName = itr.get();
                Value field = objRoot.getField(fieldName);
                Tag tag = field.getTag();
                if (isGCRoot(tag))
                    toMark.push_back((refptr)field);
            }
        }
        else if (tag == TAG_ARRAY) 
        {   
            Array arrRoot = (Array)Value(ptr, tag);
            // std::cout << "arr" << std::endl;
            auto len = arrRoot.length();
            for(size_t i = 0; i < len; i++)
            {
                Value field = arrRoot.getElem(i);
                Tag tag = field.getTag();
                if (isGCRoot(tag)) 
                    toMark.push_back((refptr)field);
            }
        }
        if (tag == TAG_OBJECT || tag == TAG_ARRAY)
        {
            auto header = *(uint64_t*)ptr;
            if (header & HEADER_MSK_NEXT)
            {
                auto nextPtr = *(refptr*)(ptr + OBJ_OF_NEXT);
                assert (nextPtr != nullptr);
                toMark.push_back(nextPtr);
            }
        }
        if (tag == TAG_IMGREF)
        {
            ImgRef imgRef = (ImgRef)Value(ptr, tag);
            refptr strPtr = imgRef.getStringPtr();
            toMark.push_back(strPtr);
        }
    }
}

void VM::sweep() 
{
    std::vector<refptr> oldValues = values;
    values = {};
    for (refptr ptr : oldValues) 
    {
        auto header = *(uint64_t*)ptr;
        if (!(header & HEADER_MSK_MARK))
        {
            Tag tag = *(Tag*)ptr;
            if (tag == TAG_STRING)
            {
                String str = Value(ptr, tag);
                stringPool.removeString((std::string)str);
            }
            *(uint64_t*)(ptr) = header & (~HEADER_MSK_NEXT);
            free(ptr);
        } 
        else 
        {
            clearMark(ptr);
            values.push_back(ptr);
        }
    }
}

void Wrapper::setNextPtr(refptr obj, refptr nextPtr)
{
    // Get the object header
    auto header = *(obj_header*)obj;

    // Set the next pointer
    *(refptr*)(obj + OBJ_OF_NEXT) = nextPtr;

    // Set the next pointer flag bit in the object header
    *(obj_header*)(obj) = header | HEADER_MSK_NEXT;
}

refptr Wrapper::getNextPtr(refptr obj, refptr notFound)
{
    auto header = *(obj_header*)obj;
    bool hasNextPtr = header & HEADER_MSK_NEXT;

    if (!hasNextPtr)
        return notFound;

    auto nextPtr = *(refptr*)(obj + OBJ_OF_NEXT);
    assert (nextPtr != nullptr);

    return nextPtr;
}

refptr Wrapper::getObjPtr()
{
    auto objPtr = (refptr)val;
    assert (objPtr != nullptr);
    return getNextPtr(objPtr, objPtr);
}

String::String(std::string str)
{
    val = stringPool.getString(str);
}

String::String(Value value)
{
    assert (value.isString());
    val = value;
}

uint32_t String::length() const
{
    auto ptr = (refptr)val;
    assert (ptr != nullptr);
    auto len = *(uint32_t*)(ptr + OF_LEN);
    return len;
}

const char* String::getDataPtr() const
{
    auto ptr = (refptr)val;
    assert (ptr != nullptr);
    auto strdata = (char*)(ptr + OF_DATA);
    return strdata;
}

/// Casting operator to extract a string value
String::operator std::string ()
{
    auto len = length();
    const char* dataptr = getDataPtr();

    return std::string(dataptr, len);
}

bool String::operator == (const char* that) const
{
    return strcmp(getDataPtr(), that) == 0;
}

/// Get the ith character code
char String::operator [] (size_t i)
{
    auto len = length();
    auto strdata = getDataPtr();
    assert (i < len);
    return strdata[i];
}

String String::concat(String a, String b)
{
    auto lenA = a.length();
    auto lenB = b.length();

    std::string c;
    c.resize(lenA + lenB);

    for (size_t i = 0; i < lenA; ++i)
        c[i] = a[i];

    for (size_t i = 0; i < lenB; ++i)
        c[lenA + i] = b[i];

    return String(c);
}

/// Allocate a new array of a given length
Array::Array(size_t minCap)
{
    // Compute the object size
    auto numBytes = memSize(minCap);

    // Allocate memory
    val = vm.alloc(numBytes, TAG_ARRAY);
    auto ptr = (refptr)val;

    // Set the array capacity and length
    *(uint32_t*)(ptr + OF_CAP) = minCap;
    *(uint32_t*)(ptr + OF_LEN) = 0;

    // No initialization necessary because vm.alloc
    // provides zeroed out memory, initialized to all zeroes,
    // which evaluates to $undef.
}

Array::Array(Value value)
{
    assert (value.isArray());
    this->val = value;
}

size_t Array::getCap()
{
    auto ptr = getObjPtr();
    auto cap = *(uint32_t*)(ptr + OF_CAP);
    return cap;
}

uint32_t Array::length()
{
    auto ptr = getObjPtr();
    auto len = *(uint32_t*)(ptr + OF_LEN);
    return len;
}

/// Set the value of the ith element
void Array::setElem(size_t i, Value v)
{
    auto ptr = getObjPtr();
    auto cap = getCap();

    auto words = (Word*)(ptr + OF_DATA);
    auto tags  = (Tag*) (ptr + OF_DATA + cap * sizeof(Word));

    assert (length() <= getCap());
    assert (i < length());
    words[i] = v.getWord();
    tags[i] = v.getTag();
}

/// Get the value of the ith element
Value Array::getElem(size_t i)
{
    auto ptr = getObjPtr();
    auto cap = getCap();

    auto words = (Word*)(ptr + OF_DATA);
    auto tags  = (Tag*) (ptr + OF_DATA + cap * sizeof(Word));

    assert (length() <= getCap());
    assert (i < length());
    auto word = words[i];
    auto tag = tags[i];

    return Value(word, tag);
}

void Array::push(Value val)
{
    auto ptr = getObjPtr();
    auto cap = getCap();
    auto len = length();
    assert (len <= cap);

    // If the array is at capacity
    if (len == cap)
    {
        // Create a new array with twice the capacity
        auto newCap = 2 * cap + 1;
        //std::cerr << "extending array capacity from " << cap << " to " << newCap << std::endl;
        auto newArr = Array(newCap);

        // Copy elements to the new array
        for (size_t i = 0; i < len; ++i)
            newArr.push(getElem(i));

        // Set the next pointer on this object
        auto rootObjPtr = (refptr)this->val;
        setNextPtr(rootObjPtr, newArr.getObjPtr());
        assert (getObjPtr() != ptr);

        ptr = getObjPtr();
        cap = newCap;

        //std::cout << "done extending array" << std::endl;
    }

    auto words = (Word*)(ptr + OF_DATA);
    auto tags  = (Tag*) (ptr + OF_DATA + cap * sizeof(Word));

    words[len] = val.getWord();
    tags[len] = val.getTag();

    // Increment the length
    *(uint32_t*)(ptr + OF_LEN) = len + 1;
}

Value Array::pop()
{
    auto ptr = getObjPtr();
    auto cap = getCap();
    auto len = length();
    assert (len > 0);

    auto words = (Word*)(ptr + OF_DATA);
    auto tags  = (Tag*) (ptr + OF_DATA + cap * sizeof(Word));

    auto word = words[len-1];
    auto tag = tags[len-1];

    // Increment the length
    *(uint32_t*)(ptr + OF_LEN) = len - 1;

    return Value(word, tag);
}

/// Allocate a new empty object
Object::Object(size_t cap)
{
    if (cap < MIN_CAP)
        cap = MIN_CAP;

    // Compute the object size
    auto numBytes = memSize(cap);

    // Allocate memory
    val = vm.alloc(numBytes, TAG_OBJECT);
    auto ptr = (refptr)val;

    // Set the object capacity
    *(uint32_t*)(ptr + OF_CAP) = cap;
    // TODO: init object shape, when shapes actually implemented!

    // No field initialization necessary
}

Object::Object(Value value)
{
    assert (value.getTag() == TAG_OBJECT);
    assert ((refptr)value != nullptr);
    val = value;
}

size_t Object::getCap()
{
    auto ptr = getObjPtr();
    auto cap = *(uint32_t*)(ptr + OF_CAP);
    assert (cap > 0);
    return cap;
}

size_t Object::getSlotIdx(
    refptr ptr,
    size_t cap,
    String fieldName,
    bool newField
)
{
    auto values = (Value*)(ptr + OF_FIELDS);

    // FIXME: for now, we use a dumb key-pair linear search strategy
    size_t idx = 0;
    for (; idx < cap; idx += 2)
    {
        // Empty slot, property name not found
        if (values[idx] == Value::UNDEF)
            return newField? idx:cap;

        assert (values[idx].isString());

        // Slot found
        if ((String)values[idx] == fieldName)
            return idx;
    }

    return cap;
}

bool Object::hasField(String fieldName)
{
    auto ptr = getObjPtr();
    auto cap = getCap();

    auto slotIdx = getSlotIdx(ptr, cap, fieldName, false);

    return (slotIdx < cap);
}

void Object::setField(String name, Value value)
{
    auto ptr = getObjPtr();
    auto cap = getCap();

    auto slotIdx = getSlotIdx(ptr, cap, name, true);

    // If we've exceeded the object capacity
    if (slotIdx + 1 >= cap)
    {
        // Create a new object with twice the capacity
        assert (cap > 0);
        auto newCap = 2 * cap;
        //std::cout << "extending object capacity from " << cap << " to " << newCap << std::endl;
        auto newObj = Object(newCap);

        // Copy properties to the new object
        for (auto itr = ObjFieldItr(*this); itr.valid(); itr.next())
        {
            auto fieldName = itr.get();
            auto fieldVal = getField(fieldName);
            newObj.setField(fieldName, fieldVal);
        }

        // Set the next pointer on this object
        auto newObjPtr = newObj.getObjPtr();
        auto rootObjPtr = (refptr)val;
        setNextPtr(rootObjPtr, newObjPtr);
        assert (getObjPtr() != ptr);

        ptr = getObjPtr();
        cap = getCap();

    }
    // Write the new property
    assert (slotIdx + 1 < cap);
    auto values = (Value*)(ptr + OF_FIELDS);
    values[slotIdx + 0] = name;
    values[slotIdx + 1] = value;
}

Value Object::getField(String name)
{
    auto ptr = getObjPtr();
    auto cap = getCap();
    auto values = (Value*)(ptr + OF_FIELDS);

    size_t slotIdx = getSlotIdx(ptr, cap, name, false);

    assert (slotIdx < cap);
    return values[slotIdx + 1];
}

bool Object::getField(const String& fieldName, Value& value, size_t& idxCache)
{
    auto ptr = getObjPtr();
    auto cap = getCap();
    auto values = (Value*)(ptr + OF_FIELDS);

    //std::cout << "Lookup" << std::endl;
    //std::cout << "  name=" << name << std::endl;
    //std::cout << "  idxCache=" << idxCache << std::endl;

    if (idxCache < cap)
    {
        auto nameSlot = values[idxCache];
        if (nameSlot.isString())
        {
            auto nameSlotStr = String(nameSlot);
            if (nameSlotStr == fieldName)
            {
                //std::cout << "  cache hit" << std::endl;
                value = values[idxCache + 1];
                return true;
            }
        }
    }

    //std::cout << "cache miss" << std::endl;
    //std::cout << "  name=" << name << std::endl;

    size_t slotIdx = getSlotIdx(ptr, cap, fieldName, false);

    if (slotIdx >= cap)
    {
        return false;
    }

    idxCache = slotIdx;

    //std::cout << "  cache miss" << std::endl;
    //std::cout << "  slotIdx=" << slotIdx << std::endl;

    value = values[slotIdx + 1];
    return true;
}

int32_t Object::getFieldInt32(std::string name)
{
    if (!hasField(name))
        throw RunError("missing field \"" + name + "\"");

    auto val = getField(name);

    if (!val.isInt32())
        throw RunError("expected field \"" + name + "\" to have type int32");

    return int32_t(val);
}

Object Object::getFieldObj(std::string name)
{
    if (!hasField(name))
        throw RunError("missing field \"" + name + "\"");

    auto val = getField(name);

    if (!val.isObject())
        throw RunError("expected field \"" + name + "\" to have object type");

    return Object(val);
}

Array Object::getFieldArr(std::string name)
{
    if (!hasField(name))
        throw RunError("missing field \"" + name + "\"");

    auto val = getField(name);

    if (!val.isArray())
        throw RunError("expected field \"" + name + "\" to have array type");

    return Array(val);
}

ObjFieldItr::ObjFieldItr(Object obj)
: obj(obj)
{
}

bool ObjFieldItr::valid()
{
    //std::cout << "valid" << std::endl;

    auto ptr = obj.getObjPtr();
    auto cap = obj.getCap();
    auto values = (Value*)(ptr + Object::OF_FIELDS);

    return slotIdx < cap && values[slotIdx].isString();
}

std::string ObjFieldItr::get()
{
    auto ptr = obj.getObjPtr();
    auto values = (Value*)(ptr + Object::OF_FIELDS);

    //std::cout << "cap=" << cap << std::endl;
    //std::cout << "tag=" << (int)values[slotIdx].getTag() << std::endl;

    assert (values[slotIdx].isString());

    return values[slotIdx];
}

void ObjFieldItr::next()
{
    auto ptr = obj.getObjPtr();
    auto cap = obj.getCap();
    auto values = (Value*)(ptr + Object::OF_FIELDS);

    slotIdx += 2;

    if (slotIdx >= cap)
        return;

    if (values[slotIdx] == Value::UNDEF)
        slotIdx = cap;
}

ImgRef::ImgRef(String symbol)
{
    // Allocate memory
    val = vm.alloc(ImgRef::SIZE, TAG_IMGREF);
    auto ptr = (refptr)val;

    // Set the string pointer
    *(refptr*)(ptr + OF_SYM) = (refptr)symbol;
}

ImgRef::ImgRef(Value val)
{
    assert (val.getTag() == TAG_IMGREF);
    this->val = val;
}

refptr ImgRef::getStringPtr() const {
    auto ptr = (refptr)val;
    assert (ptr != nullptr);

    return *(refptr*)(ptr + OF_SYM);
}

std::string ImgRef::getName() const
{
    auto ptr = (refptr)val;
    assert (ptr != nullptr);

    auto strPtr = *(refptr*)(ptr + OF_SYM);
    Value strVal = Value(strPtr, TAG_STRING);

    return (std::string)strVal;
}

//Murmurhash, learn more at: https://en.wikipedia.org/wiki/MurmurHash
int64_t murmurHash2(const void* key, size_t len, uint64_t seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    uint64_t* data = (uint64_t*)key;
    uint64_t* end = data + (len/8);

    while (data != end)
    {
        uint64_t k = *(data++);

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    uint8_t* tail = (uint8_t*)data;

    switch (len & 7)
    {
        case 7: h ^= ((uint64_t)tail[6]) << 48;
        case 6: h ^= ((uint64_t)tail[5]) << 40;
        case 5: h ^= ((uint64_t)tail[4]) << 32;
        case 4: h ^= ((uint64_t)tail[3]) << 24;
        case 3: h ^= ((uint64_t)tail[2]) << 16;
        case 2: h ^= ((uint64_t)tail[1]) << 8;
        case 1: h ^= ((uint64_t)tail[0]);
                h *= m;
        default:;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    return h;
}

StringPool::StringPool()
{
}

Value StringPool::getString(std::string str)
{
    auto iter = pool.find(str);
    if (iter == pool.end())
    {
        return newString(str);
    }
    return Value(iter->second, TAG_STRING);
}

void StringPool::removeString(std::string str)
{
    pool.erase(str);
}

Value StringPool::newString(std::string str)
{
    auto len = str.length();
    // Compute the string object size
    auto numBytes = String::memSize(len);

    // Allocate memory
    auto val = vm.alloc(numBytes, TAG_STRING);
    auto ptr = (refptr)val;

    // Set the string length
    *(uint32_t*)(ptr + String::OF_LEN) = len;

    // Copy the string data
    strcpy((char*)(ptr + String::OF_DATA), str.c_str());
    pool.insert({str, ptr});
    return val;
}

bool isValidIdent(std::string identStr)
{
    if (identStr.length() == 0)
        return false;

    // First character must be underscore or a letter
    if (identStr[0] != '_' && !isalpha(identStr[0]))
    {
        return false;
    }

    // Remaining characters must be underscore or alphanumerical
    for (size_t i = 1; i < identStr.length(); ++i)
    {
        auto ch = identStr[i];
        if (!isalnum(ch) && ch != '_')
            return false;
    }

    return true;
}

Tag strToTag(std::string str)
{
    if (str == "undef")     return TAG_UNDEF;
    if (str == "bool")      return TAG_BOOL;
    if (str == "int32")     return TAG_INT32;
    if (str == "int64")     return TAG_INT64;
    if (str == "float32")   return TAG_FLOAT32;
    if (str == "float64")   return TAG_FLOAT64;
    if (str == "string")    return TAG_STRING;
    if (str == "object")    return TAG_OBJECT;
    if (str == "array")     return TAG_ARRAY;
    if (str == "hostfn")    return TAG_HOSTFN;
    assert (false);
}

std::string tagToStr(Tag tag)
{
    switch (tag)
    {
        case TAG_UNDEF:     return "undef";
        case TAG_BOOL:      return "bool";
        case TAG_INT32:     return "int32";
        case TAG_INT64:     return "int64";
        case TAG_FLOAT32:   return "float32";
        case TAG_FLOAT64:   return "float64";
        case TAG_STRING:    return "string";
        case TAG_OBJECT:    return "object";
        case TAG_ARRAY:     return "array";
        case TAG_HOSTFN:    return "hostfn";
        case TAG_RAWPTR:    return "rawptr";
        default:
        assert (false);
    }
}

std::string posToString(Value srcPos)
{
    assert (srcPos.isObject());
    auto srcPosObj = (Object)srcPos;

    auto lineNo = (int32_t)srcPosObj.getField("line_no");
    auto colNo = (int32_t)srcPosObj.getField("col_no");
    auto srcName = (std::string)srcPosObj.getField("src_name");

    return (
        srcName + "@" +
        std::to_string(lineNo) + ":" +
        std::to_string(colNo)
    );
}

/// Unit test for the runtime
void testRuntime()
{
    std::cout << "runtime tests" << std::endl;

    // Strings
    auto str = String("foobar");
    assert (str.length() == 6);
    assert ((std::string)str == "foobar");
    assert (str[1] == 'o');
    auto str2 = String("foobar");
    assert (str.length() == 6);
    assert (str2.length() == 6);
    assert (str == str2);
    assert ((std::string)str == (std::string)str2);

    // Arrays
    auto arr = Array(2);
    assert (arr.length() == 0);
    arr.push(Value::ONE);
    assert (arr.length() == 1);
    assert (arr.getElem(0) == Value::ONE);
    arr.push(Value::TWO);
    assert (arr.length() == 2);
    assert (arr.getElem(0) == Value::ONE);
    assert (arr.getElem(1) == Value::TWO);
    arr.setElem(0, Value::ZERO);
    assert (arr.length() == 2);
    assert (arr.getElem(0) == Value::ZERO);

    // Array extension
    auto arr2 = Array(0);
    assert (arr2.length() == 0);
    arr2.push(Value::ONE);
    assert (arr2.length() == 1);
    assert (arr2.getElem(0) == Value::ONE);
    arr2.push(Value::TWO);
    assert (arr2.length() == 2);
    assert (arr2.getElem(0) == Value::ONE);
    assert (arr2.getElem(1) == Value::TWO);

    // Regression test: changing element tag
    auto arr3 = Array(2);
    arr3.push(Value::ZERO);
    arr3.push(Value::ONE);
    arr3.push(Value::TWO);
    arr3.setElem(0, Value::TRUE);
    assert (arr3.length() == 3);
    assert (arr3.getElem(0) == Value::TRUE);
    assert (arr3.getElem(1) == Value::ONE);
    assert (arr3.getElem(2) == Value::TWO);
    auto val = arr3.pop();
    assert (val == Value::TWO);
    assert (arr3.length() == 2);

    // Objects
    auto obj = Object();
    assert (!obj.hasField("foo"));
    obj.setField("foo", Value::ONE);
    obj.setField("bar", Value::TWO);
    assert (obj.getField("foo") == Value::ONE);
    assert (obj.getField("bar") == Value::TWO);
    obj.setField("foo", Value::TRUE);
    assert (obj.getField("foo") == Value::TRUE);
    assert (obj.getField("bar") == Value::TWO);
    assert (obj.hasField("foo"));
    assert (obj.hasField("bar"));

    // Field iteration
    std::string fieldStr;
    for (auto itr = ObjFieldItr(obj); itr.valid(); itr.next())
        fieldStr += itr.get();
    assert (fieldStr == "foobar");
}
