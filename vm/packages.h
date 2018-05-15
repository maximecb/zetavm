#pragma once

#include "runtime.h"

/**
Host function wrapper
*/
class HostFn
{
private:

    std::string name;

    size_t numParams;

    void* fptr;

public:

    HostFn(
        std::string name,
        size_t numParams,
        void* fptr
    );

    Value call0();
    Value call1(Value arg0);
    Value call2(Value arg0, Value arg1);
    Value call3(Value arg0, Value arg1, Value arg2);

    size_t getNumParams() const { return numParams; }

    std::string getName() const { return name; }
};

class ImportError : public RunError
{
public:

    ImportError(std::string msg) : RunError(msg) {}
    ~ImportError() {}
};

/**
Exception to signal a missing dependency
*/
class DepMissing : public ImportError
{
public:

    DepMissing(
        std::string pkgName,
        std::string libName,
        std::string confOpt
    ) : ImportError(
        "package \"" + pkgName + "\" is not available because a "
        "dependency is missing. To enable this package, please "
        "install " + libName + " on your system and then rebuild "
        "ZetaVM with " + libName + " support as follows:\n"
        "\n"
        "  ./configure " + confOpt + "\n"
        "  make clean\n"
        "  make"
    )
    {
    }

    virtual ~DepMissing()
    {
    }
};

/// User-facing import function, used to implement the import instruction
extern HostFn importFn;

/// Load a package based on its path
Object load(std::string pkgPath);

/// Import a package based on its name, and perform caching
Object import(std::string pkgName);
