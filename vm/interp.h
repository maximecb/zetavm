#pragma once

#include <vector>
#include "runtime.h"

typedef std::vector<Value> ValueVec;

/// Initialize the interpreter
void initInterp();

void markStackValues();

/// Call a function exported by a package
Value callExportFn(
    Object pkg,
    std::string fnName,
    ValueVec args = ValueVec()
);

void testInterp();
