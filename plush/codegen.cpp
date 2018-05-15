#include <cassert>
#include <vector>
#include <string>
#include <iostream>
#include "codegen.h"

// Last assigned id number
size_t lastIdNo = 0;

class Block
{
private:

    size_t idNo;

    std::vector<std::string> instrs;

    bool finalized = false;

public:

    Block()
    {
        idNo = lastIdNo++;
    }

    std::string getHandle() const { return "block_" + std::to_string(idNo); }

    bool isFinalized() const { return finalized; }

    void add(std::string instrStr)
    {
        if (finalized)
        {
            std::cout << "block is finalized:" << std::endl;
            for (auto str: instrs)
                std::cout << "    " + str << std::endl;
            std::cout << "cannot add:" << std::endl;
            std::cout << instrStr << std::endl;
            exit(-1);
        }

        instrs.push_back("{ " + instrStr + " }");
    };

    void finalize(std::string& out)
    {
        assert (!finalized);
        assert (instrs.size() > 0);

        out += getHandle() + " = {\n";
        out += "  instrs: [\n";

        for (auto str: instrs)
            out += "    " + str + ",\n";

        out += "  ]\n";
        out += "};\n\n";

        finalized = true;
    }
};

struct Function
{
private:

    size_t idNo;

    // Visible function parameter names
    std::vector<std::string> params;

    // Functions always have at least 1 local
    // to store the hidden function/closure argument
    size_t numLocals = 1;

    Block* entryBlock;

    /// Local variable indices
    std::unordered_map<std::string, size_t> localIdxs;

    std::string name;

public:

    Function(
        std::vector<std::string> params,
        Block* entryBlock,
        const std::string& nname
    )
    : params(params),
      entryBlock(entryBlock),
      name(nname)
    {
        idNo = lastIdNo++;
    }

    std::string getHandle() { return "fun_" + std::to_string(idNo); }

    /// Register a local variable declaration
    void registerDecl(std::string identName)
    {
        assert (entryBlock != nullptr);

        if (localIdxs.find(identName) == localIdxs.end())
        {
            size_t newIdx = localIdxs.size();
            localIdxs[identName] = newIdx;
            numLocals++;
        }
    }

    bool hasLocal(std::string identName)
    {
        return (localIdxs.find(identName) != localIdxs.end());
    }

    size_t getLocalIdx(std::string identName)
    {
        assert (hasLocal(identName));
        return localIdxs[identName];
    }

    void finalize(std::string& out)
    {
        assert (entryBlock != nullptr);

        std::string paramsStr = "[";
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto param = params[i];
            paramsStr += "\'" + param + "\'";
            if (i < params.size() - 1)
                paramsStr += ", ";

        }
        paramsStr += "]";

        out += getHandle() + " = {\n";
        out += "  entry:@" + entryBlock->getHandle() + ",\n";
        out += "  params:" + paramsStr + ",\n";
        out += "  num_locals:" + std::to_string(numLocals) + ",\n";
        if (name != "")
        {
            out += "  name: \'" + name + "\',\n";
        }
        out += "};\n\n";

        entryBlock = nullptr;
    }
};

class CodeGenCtx
{
public:

    std::string& out;

    /// Current function being generated
    Function* fun;

    /// Current block into which to insert
    Block* curBlock;

    /// Loop continue block
    Block* contBlock;

    /// Loop break block
    Block* breakBlock;

    /// Try catch block
    Block* catchBlock;

    /// Unit function flag
    bool unitFun;

    CodeGenCtx(
        std::string& out,
        Function* fun,
        bool unitFun,
        Block* curBlock,
        Block* contBlock = nullptr,
        Block* breakBlock = nullptr,
        Block* catchBlock = nullptr
    )
    : out(out),
      fun(fun),
      curBlock(curBlock),
      contBlock(contBlock),
      breakBlock(breakBlock),
      catchBlock(catchBlock),
      unitFun(unitFun)
    {
    }

    /// Create a sub-context of this context
    CodeGenCtx subCtx(
        Block* startBlock = nullptr,
        Block* contBlock = nullptr,
        Block* breakBlock = nullptr,
        Block* catchBlock = nullptr
    )
    {
        return CodeGenCtx(
            this->out,
            this->fun,
            this->unitFun,
            startBlock? startBlock:this->curBlock,
            contBlock? contBlock:this->contBlock,
            breakBlock? breakBlock:this->breakBlock,
            catchBlock? catchBlock:this->catchBlock
        );
    }

    /// Continue code generation at a given block
    void merge(Block* block)
    {
        curBlock = block;
    }

    /// Add an instruction string
    void addStr(std::string instrStr)
    {
        assert (curBlock != nullptr);
        curBlock->add(instrStr);
    }

    /// Add an instruction with no arguments by opcode name
    void addOp(std::string opStr)
    {
        addStr("op:'" + opStr + "'");
    }

    /// Add a push instruction
    void addPush(std::string valStr)
    {
        addStr("op:'push', val:" + valStr);
    }

    /// Add a branch instruction
    void addBranch(
        std::string op,
        std::string name0 = "",
        Block* target0 = nullptr,
        std::string name1 = "",
        Block* target1 = nullptr,
        std::string extraArgs = ""
    )
    {
        assert (curBlock != nullptr);

        if (target0 && target1)
        {
            addStr(
                "op:'" + op + "', " +
                name0 + ":@" + target0->getHandle() + ", " +
                name1 + ":@" + target1->getHandle() +
                (extraArgs.length()? ", ":"") + extraArgs
            );
        }
        else if (target0)
        {
            addStr(
                "op:'" + op + "', " + name0 + ":@" + target0->getHandle() +
                (extraArgs.length()? ", ":"") + extraArgs
            );
        }
        else
        {
            addStr(
                "op:'" + op + "'" +
                (extraArgs.length()? ", ":"") + extraArgs
            );
        }

        /// Serialize the basic block
        curBlock->finalize(this->out);
    }
};

/**
Register variable declarations within a function body
*/
void registerDecls(Function* fun, ASTStmt* stmt, bool unitFun)
{
    if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt))
    {
        for (auto stmt : blockStmt->stmts)
            registerDecls(fun, stmt, unitFun);
        return;
    }

    if (auto varStmt = dynamic_cast<VarStmt*>(stmt))
    {
        // If this is not a unit function, create a new local
        if (!unitFun)
            fun->registerDecl(varStmt->identName);
        return;
    }

    if (dynamic_cast<ExprStmt*>(stmt) != nullptr)
    {
        return;
    }

    if (auto ifStmt = dynamic_cast<IfStmt*>(stmt))
    {
        registerDecls(fun, ifStmt->thenStmt, unitFun);
        registerDecls(fun, ifStmt->elseStmt, unitFun);
        return;
    }

    if (auto forStmt = dynamic_cast<ForStmt*>(stmt))
    {
        registerDecls(fun, forStmt->initStmt, unitFun);
        registerDecls(fun, forStmt->bodyStmt, unitFun);
        return;
    }

    if (dynamic_cast<ContStmt*>(stmt) != nullptr)
    {
        return;
    }

    if (dynamic_cast<BreakStmt*>(stmt) != nullptr)
    {
        return;
    }

    if (auto tryStmt = dynamic_cast<TryStmt*>(stmt))
    {
        registerDecls(fun, tryStmt->bodyStmt, unitFun);
        registerDecls(fun, tryStmt->catchStmt, unitFun);

        // If this is not a unit function, create a new local
        if (!unitFun)
            fun->registerDecl(tryStmt->catchVar);

        return;
    }

    if (dynamic_cast<ReturnStmt*>(stmt) != nullptr)
    {
        return;
    }

    if (dynamic_cast<ThrowStmt*>(stmt) != nullptr)
    {
        return;
    }

    if (dynamic_cast<IRStmt*>(stmt) != nullptr)
    {
        return;
    }

    assert (false && "unhandled statement type in registerDecls");
}

// Forward declarations
void genExpr(CodeGenCtx& ctx, ASTExpr* expr);
void genStmt(CodeGenCtx& ctx, ASTStmt* stmt);
void genLogicalAnd(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr);
void genLogicalOr(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr);
void genObjExpr(CodeGenCtx& ctx, ASTExpr* protoExpr, ObjectExpr* objExpr);
void genAssign(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr);

/**
Generate code for a code unit
*/
std::string genUnit(FunExpr* unitAST)
{
    std::string out = "#zeta-image\n\n";

    Block* entryBlock = new Block();

    Function* unitFun = new Function(std::vector<std::string>(), entryBlock, "top-level");

    // Register the variable declarations
    registerDecls(unitFun, unitAST->body, true);

    // Create the initial context
    CodeGenCtx ctx(
        out,
        unitFun,
        true,
        entryBlock
    );

    // Define the global and exports objects
    out += "exports_obj = { init: @" + unitFun->getHandle() + " };\n";
    out += "global_obj = { exports: @exports_obj };\n\n";

    // Generate code for the function body
    genStmt(ctx, unitAST->body);

    // Add a final return statement to the unit function
    if (!ctx.curBlock->isFinalized())
    {
        ctx.addStr("op:'push', val:$true");
        ctx.addBranch("ret");
    }

    // Generate output for the unit function
    unitFun->finalize(out);

    // Export the exports object
    out += "@exports_obj;\n";

    return out;
}

void runtimeCall(CodeGenCtx& ctx, std::string funName, size_t numArgs)
{
    ctx.addStr("op:'push', val:@global_obj");
    ctx.addStr("op:'push', val:'rt_" + funName + "'");
    ctx.addOp("get_field");

    auto contBlock = new Block();
    ctx.addBranch(
        "call",
        "ret_to",
        contBlock,
        ctx.catchBlock? "throw_to":"",
        ctx.catchBlock,
        "num_args:" + std::to_string(numArgs)
    );
    ctx.merge(contBlock);
}

void genExpr(CodeGenCtx& ctx, ASTExpr* expr)
{
    if (auto intExpr = dynamic_cast<IntExpr*>(expr))
    {
        ctx.addStr("op:'push', val:" + std::to_string(intExpr->val));
        return;
    }

    if (auto floatExpr = dynamic_cast<FloatExpr*>(expr))
    {
        ctx.addStr("op:'push', val:" + std::to_string(floatExpr->val) + "f");
        return;
    }

    if (auto strExpr = dynamic_cast<StringExpr*>(expr))
    {
        // Escape the string conservatively
        std::string escStr;
        for (unsigned char ch : strExpr->val)
        {
            if (ch < 32 || ch > 126)
            {
                auto d0 = ch / 16;
                auto d1 = ch % 16;
                escStr += "\\x";
                escStr += (d0 < 10)? ('0' + d0):('A' + d0 - 10);
                escStr += (d1 < 10)? ('0' + d1):('A' + d1 - 10);
            }
            else if (ch == '\'')
            {
                escStr += "\\'";
            }
            else if (ch == '\"')
            {
                escStr += "\\\"";
            }
            else if (ch == '\\')
            {
                escStr += "\\\\";
            }
            else
            {
                escStr += ch;
            }
        }

        ctx.addStr("op:'push', val:'" + escStr + "'");
        return;
    }

    if (auto identExpr = dynamic_cast<IdentExpr*>(expr))
    {
        if (identExpr->name == "true")
        {
            ctx.addStr("op:'push', val:$true");
            return;
        }

        if (identExpr->name == "false")
        {
            ctx.addStr("op:'push', val:$false");
            return;
        }

        if (identExpr->name == "undef")
        {
            ctx.addStr("op:'push', val:$undef");
            return;
        }

        if (ctx.fun->hasLocal(identExpr->name))
        {
            size_t localIdx = ctx.fun->getLocalIdx(identExpr->name);
            ctx.addStr("op:'get_local', idx:" + std::to_string(localIdx));
        }
        else
        {
            ctx.addStr("op:'push', val:@global_obj");
            ctx.addStr("op:'push', val:'" + identExpr->name + "'");
            ctx.addOp("get_field");
        }

        return;
    }

    if (auto unOp = dynamic_cast<UnOpExpr*>(expr))
    {
        // Logical not
        if (unOp->op == &OP_NOT)
        {
            genExpr(ctx, unOp->expr);
            runtimeCall(ctx, "not", 1);
            return;
        }

        // Unary negation
        if (unOp->op == &OP_NEG)
        {
            // Generate 0 - x
            ctx.addPush("0");
            genExpr(ctx, unOp->expr);
            runtimeCall(ctx, "sub", 2);
            return;
        }

        // Bitwise not; one's-complement, or flipping each bit
        if (unOp->op == &OP_BIT_NOT)
        {
            genExpr(ctx, unOp->expr);
            runtimeCall(ctx, "bit_not", 1);
            return;
        }

        if (unOp->op == &OP_TYPEOF)
        {
            genExpr(ctx, unOp->expr);
            ctx.addOp("get_tag");
            return;
        }

        assert (false && "unhandled unary op");
    }

    if (auto binOp = dynamic_cast<BinOpExpr*>(expr))
    {
        if (binOp->op == &OP_ASSIGN)
        {
            genAssign(ctx, binOp->lhsExpr, binOp->rhsExpr);
            return;
        }

        if (binOp->op == &OP_AND)
        {
            genLogicalAnd(ctx, binOp->lhsExpr, binOp->rhsExpr);
            return;
        }

        if (binOp->op == &OP_OR)
        {
            genLogicalOr(ctx, binOp->lhsExpr, binOp->rhsExpr);
            return;
        }

        if (binOp->op == &OP_EQ)
        {
            // Expression of the form: typeof x == "type_string"
            if (auto unOp = dynamic_cast<UnOpExpr*>(binOp->lhsExpr))
            {
                if (unOp->op == &OP_TYPEOF)
                {
                    if (auto strExpr = dynamic_cast<StringExpr*>(binOp->rhsExpr))
                    {
                        genExpr(ctx, unOp->expr);
                        ctx.addStr("op:'has_tag', tag:'" + strExpr->val + "'");
                        return;
                    }
                }
            }

            // Equality comparison
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "eq", 2);
            return;
        }

        // Inequality comparison
        if (binOp->op == &OP_NE)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "ne", 2);
            return;
        }

        if (binOp->op == &OP_LT)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "lt", 2);
            return;
        }

        if (binOp->op == &OP_LE)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "le", 2);
            return;
        }

        if (binOp->op == &OP_GT)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "gt", 2);
            return;
        }

        if (binOp->op == &OP_GE)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "ge", 2);
            return;
        }

        if (binOp->op == &OP_IN)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "in", 2);
            return;
        }

        if (binOp->op == &OP_INSTOF)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "instOf", 2);
            return;
        }

        if (binOp->op == &OP_ADD)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "add", 2);
            return;
        }

        if (binOp->op == &OP_SUB)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "sub", 2);
            return;
        }

        if (binOp->op == &OP_MUL)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "mul", 2);
            return;
        }

        if (binOp->op == &OP_DIV)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "div", 2);
            return;
        }

        if (binOp->op == &OP_MOD)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "mod", 2);
            return;
        }

        if (binOp->op == &OP_BIT_SHL)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "shl", 2);
            return;
        }

        if (binOp->op == &OP_BIT_SHR)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "shr", 2);
            return;
        }

        if (binOp->op == &OP_BIT_USHR)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "ushr", 2);
            return;
        }

        if (binOp->op == &OP_BIT_AND)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "and", 2);
            return;
        }

        if (binOp->op == &OP_BIT_OR)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "or", 2);
            return;
        }

        if (binOp->op == &OP_BIT_XOR)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "xor", 2);
            return;
        }

        if (binOp->op == &OP_MEMBER)
        {
            genExpr(ctx, binOp->lhsExpr);

            auto identExpr = dynamic_cast<IdentExpr*>(binOp->rhsExpr);
            if (!identExpr)
                throw ParseError("invalid rhs in member expression");
            ctx.addStr("op:'push', val:'" + identExpr->name + "'");

            runtimeCall(ctx, "getProp", 2);
            return;
        }

        // Indexing operator: a[b]
        if (binOp->op == &OP_INDEX)
        {
            genExpr(ctx, binOp->lhsExpr);
            genExpr(ctx, binOp->rhsExpr);
            runtimeCall(ctx, "getElem", 2);
            return;
        }

        // Object extension
        if (binOp->op == &OP_OBJ_EXT)
        {
            auto objExpr = dynamic_cast<ObjectExpr*>(binOp->rhsExpr);
            if (!objExpr)
                throw ParseError("invalid rhs in objext extension expression");
            genObjExpr(ctx, binOp->lhsExpr, objExpr);
            return;
        }

        assert (false && "unhandled binary op");
    }

    // Object literal expression
    if (auto objExpr = dynamic_cast<ObjectExpr*>(expr))
    {
        genObjExpr(ctx, nullptr, objExpr);
        return;
    }

    // Array literal expression
    if (auto arrExpr = dynamic_cast<ArrayExpr*>(expr))
    {
        // Create a new array with a sufficient capacity
        ctx.addStr("op:'push', val:" + std::to_string(arrExpr->exprs.size()));
        ctx.addOp("new_array");

        // For each property
        for (size_t i = 0; i < arrExpr->exprs.size(); ++i)
        {
            // Duplicate the array value
            ctx.addStr("op:'dup', idx:0");

            // Evaluate the property value expression
            genExpr(ctx, arrExpr->exprs[i]);

            // Append the element to the array
            ctx.addOp("array_push");
        }

        return;
    }

    // Function/closure expression
    if (auto funExpr = dynamic_cast<FunExpr*>(expr))
    {
        Block* entryBlock = new Block();

        Function* fun = new Function(
            funExpr->params,
            entryBlock,
            funExpr->name
        );

        // Register the parameter variables
        for (auto paramName : funExpr->params)
            fun->registerDecl(paramName);

        // Register the variable declarations in the function body
        registerDecls(fun, funExpr->body, false);

        CodeGenCtx funCtx(
            ctx.out,
            fun,
            false,
            entryBlock
        );

        // Generate code for the function body
        genStmt(funCtx, funExpr->body);

        if (!funCtx.curBlock->isFinalized())
        {
            // Return the undefined value
            funCtx.addStr("op:'push', val:$undef");
            funCtx.addBranch("ret");
        }

        // Generate output for the unit function
        fun->finalize(ctx.out);

        ctx.addStr("op:'push', val:@" + fun->getHandle());

        return;
    }

    // Function call expression
    if (auto callExpr = dynamic_cast<CallExpr*>(expr))
    {
        auto& args = callExpr->argExprs;

        // Evaluate the arguments in order
        for (size_t i = 0; i < args.size(); ++i)
            genExpr(ctx, args[i]);

        // Evaluate the function expression
        genExpr(ctx, callExpr->funExpr);

        auto contBlock = new Block();
        ctx.addBranch(
            "call",
            "ret_to",
            contBlock,
            ctx.catchBlock? "throw_to":"",
            ctx.catchBlock,
            "num_args:" + std::to_string(args.size())
        );
        ctx.merge(contBlock);

        return;
    }

    // Method call expression
    if (auto callExpr = dynamic_cast<MethodCallExpr*>(expr))
    {
        auto& args = callExpr->argExprs;

        // Evaluate the base expression (this value)
        genExpr(ctx, callExpr->baseExpr);

        // Evaluate the arguments in order
        for (size_t i = 0; i < args.size(); ++i)
            genExpr(ctx, args[i]);

        // Duplicate the base (this) value
        ctx.addStr("op:'dup', idx:" + std::to_string(args.size()));

        // Push the property name
        ctx.addStr("op:'push', val:'" + callExpr->nameStr + "'");

        // Get the function/method value
        runtimeCall(ctx, "getProp", 2);

        auto contBlock = new Block();
        ctx.addBranch(
            "call",
            "ret_to", contBlock,
            "", nullptr,
            "num_args:" + std::to_string(args.size()+1)
        );
        ctx.merge(contBlock);

        return;
    }

    // Inline IR expression
    if (auto irExpr = dynamic_cast<IRExpr*>(expr))
    {
        // Evaluate the arguments in the order supplied
        auto& args = irExpr->argExprs;
        for (size_t i = 0; i < args.size(); ++i)
            genExpr(ctx, args[i]);

        ctx.addOp(irExpr->opName);

        return;
    }

    if (auto importExpr = dynamic_cast<ImportExpr*>(expr))
    {
        ctx.addStr("op:'push', val:'" + importExpr->pkgName + "'");

        auto contBlock = new Block();
        ctx.addBranch(
            "import",
            "ret_to",
            contBlock,
            ctx.catchBlock? "throw_to":"",
            ctx.catchBlock
        );
        ctx.merge(contBlock);

        return;
    }

    assert (false);
}

void genStmt(CodeGenCtx& ctx, ASTStmt* stmt)
{
    if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt))
    {
        // For each statement
        for (auto stmt : blockStmt->stmts)
        {
            genStmt(ctx, stmt);

            if (ctx.curBlock->isFinalized())
                break;
        }

        return;
    }

    if (auto varStmt = dynamic_cast<VarStmt*>(stmt))
    {
        if (ctx.fun->hasLocal(varStmt->identName))
        {
            genExpr(ctx, varStmt->initExpr);
            size_t localIdx = ctx.fun->getLocalIdx(varStmt->identName);
            ctx.addStr("op:'set_local', idx:" + std::to_string(localIdx));
        }
        else
        {
            ctx.addStr("op:'push', val:@global_obj");
            ctx.addStr("op:'push', val:'" + varStmt->identName + "'");
            genExpr(ctx, varStmt->initExpr);
            ctx.addOp("set_field");
        }

        return;
    }

    if (auto returnStmt = dynamic_cast<ReturnStmt*>(stmt))
    {
        genExpr(ctx, returnStmt->expr);
        ctx.addBranch("ret");
        return;
    }

    if (auto throwStmt = dynamic_cast<ThrowStmt*>(stmt))
    {
        genExpr(ctx, throwStmt->expr);
        runtimeCall(ctx, "throw", 1);
        return;
    }

    if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt))
    {
        genExpr(ctx, exprStmt->expr);
        ctx.addOp("pop");
        return;
    }

    if (auto ifStmt = dynamic_cast<IfStmt*>(stmt))
    {
        // Evaluate the test expression
        genExpr(ctx, ifStmt->testExpr);

        auto thenBlock = new Block();
        auto thenCtx = ctx.subCtx(thenBlock);
        genStmt(thenCtx, ifStmt->thenStmt);

        auto elseBlock = new Block();
        auto elseCtx = ctx.subCtx(elseBlock);
        genStmt(elseCtx, ifStmt->elseStmt);

        // Insert the conditional branching instruction
        ctx.addBranch("if_true", "then", thenBlock, "else", elseBlock);

        auto joinBlock = new Block();
        ctx.merge(joinBlock);

        if (!thenCtx.curBlock->isFinalized())
            thenCtx.addBranch("jump", "to", joinBlock);
        if (!elseCtx.curBlock->isFinalized())
            elseCtx.addBranch("jump", "to", joinBlock);

        return;
    }

    // For-loop statement
    if (auto forStmt = dynamic_cast<ForStmt*>(stmt))
    {
        // Loop body and exit blocks
        auto testBlock = new Block();
        auto bodyBlock = new Block();
        auto incrBlock = new Block();
        auto exitBlock = new Block();

        // Generate the initialization statement
        genStmt(ctx, forStmt->initStmt);

        // Evaluate the test expression
        ctx.addBranch("jump", "to", testBlock);
        auto testCtx = ctx.subCtx(testBlock);
        genExpr(testCtx, forStmt->testExpr);

        // Insert the conditional branching instruction
        testCtx.addBranch("if_true", "then", bodyBlock, "else", exitBlock);

        // Generate the loop body statement
        auto bodyCtx = ctx.subCtx(
            bodyBlock,
            incrBlock,
            exitBlock
        );
        genStmt(bodyCtx, forStmt->bodyStmt);
        if (!bodyCtx.curBlock->isFinalized())
            bodyCtx.addBranch("jump", "to", incrBlock);

        // Generate the increment expression
        auto incrCtx = ctx.subCtx(incrBlock);
        genExpr(incrCtx, forStmt->incrExpr);
        incrCtx.addOp("pop");
        incrCtx.addBranch("jump", "to", testBlock);

        ctx.merge(exitBlock);

        return;
    }

    // Loop break statement
    if (dynamic_cast<ContStmt*>(stmt) != nullptr)
    {
        if (!ctx.curBlock->isFinalized())
            ctx.addBranch("jump", "to", ctx.contBlock);
        return;
    }

    // Loop break statement
    if (dynamic_cast<BreakStmt*>(stmt) != nullptr)
    {
        if (!ctx.curBlock->isFinalized())
            ctx.addBranch("jump", "to", ctx.breakBlock);
        return;
    }

    // Try/catch statement
    if (auto tryStmt = dynamic_cast<TryStmt*>(stmt))
    {
        auto bodyBlock = new Block();
        auto catchBlock = new Block();
        auto joinBlock = new Block();

        ctx.addBranch("jump", "to", bodyBlock);

        // Generate the loop body statement
        auto bodyCtx = ctx.subCtx(
            bodyBlock,
            nullptr,
            nullptr,
            catchBlock
        );
        genStmt(bodyCtx, tryStmt->bodyStmt);
        if (!bodyCtx.curBlock->isFinalized())
            bodyCtx.addBranch("jump", "to", joinBlock);

        auto catchCtx = ctx.subCtx(
            catchBlock
        );

        // Assign the exception value to the catch variable
        if (catchCtx.unitFun)
        {
            catchCtx.addStr("op:'push', val:@global_obj");
            catchCtx.addStr("op:'push', val:'" + tryStmt->catchVar + "'");
            catchCtx.addStr("op:'dup', idx:2");
            catchCtx.addOp("set_field");
            catchCtx.addOp("pop");
        }
        else
        {
            auto localIdx = ctx.fun->getLocalIdx(tryStmt->catchVar);
            catchCtx.addStr("op:'set_local', idx:" + std::to_string(localIdx));
        }

        // Generate the catch statement
        genStmt(catchCtx, tryStmt->catchStmt);
        if (!catchCtx.curBlock->isFinalized())
            catchCtx.addBranch("jump", "to", joinBlock);

        ctx.merge(joinBlock);

        return;
    }

    // IR instruction statement
    if (auto irStmt = dynamic_cast<IRStmt*>(stmt))
    {
        // Evaluate the arguments in reverse order
        auto& args = irStmt->argExprs;
        for (size_t i = 0; i < args.size(); ++i)
            genExpr(ctx, args[i]);

        ctx.addOp(irStmt->instrName);

        return;
    }

    assert (false);
}

void genLogicalAnd(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr)
{
    auto andBlock = new Block();
    auto doneBlock = new Block();

    // Evaluate the lhs expression
    genExpr(ctx, lhsExpr);
    ctx.addStr("op:'dup', idx:0");
    ctx.addBranch("if_true", "then", andBlock, "else", doneBlock);

    // Evaluate the second expression
    auto andCtx = ctx.subCtx(andBlock);
    andCtx.addOp("pop");
    genExpr(andCtx, rhsExpr);
    andCtx.addBranch("jump", "to", doneBlock);

    ctx.merge(doneBlock);
}

void genLogicalOr(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr)
{
    auto orBlock = new Block();
    auto doneBlock = new Block();

    // Evaluate the lhs expression
    genExpr(ctx, lhsExpr);
    ctx.addStr("op:'dup', idx:0");
    ctx.addBranch("if_true", "then", doneBlock, "else", orBlock);

    // If the first expression fails, evaluate the second one
    auto orCtx = ctx.subCtx(orBlock);
    orCtx.addOp("pop");
    genExpr(orCtx, rhsExpr);
    orCtx.addBranch("jump", "to", doneBlock);

    ctx.merge(doneBlock);
}

void genObjExpr(CodeGenCtx& ctx, ASTExpr* protoExpr, ObjectExpr* objExpr)
{
    assert (objExpr->names.size() == objExpr->exprs.size());

    // Create a new object
    ctx.addStr("op:'push', val:" + std::to_string(objExpr->exprs.size()));
    ctx.addOp("new_object");

    // If a prototype expression is specified
    if (protoExpr)
    {
        // Duplicate the object value
        ctx.addStr("op:'dup', idx:0");

        // Push the prototype property name
        ctx.addPush("'proto'");

        // Evaluate the prototype expression
        genExpr(ctx, protoExpr);

        // Set the prototype field
        ctx.addOp("set_field");
    }

    // For each property
    for (size_t i = 0; i < objExpr->names.size(); ++i)
    {
        // Duplicate the object value
        ctx.addStr("op:'dup', idx:0");

        // Push the property name
        ctx.addStr("op:'push', val:'" + objExpr->names[i] + "'");

        // Evaluate the property value expression
        genExpr(ctx, objExpr->exprs[i]);

        ctx.addOp("set_field");
    }
}

void genAssign(CodeGenCtx& ctx, ASTExpr* lhsExpr, ASTExpr* rhsExpr)
{
    // Assignment to a variable
    if (auto identExpr = dynamic_cast<IdentExpr*>(lhsExpr))
    {
        if (identExpr->name == "exports")
        {
            throw ParseError("cannot assign to exports variable");
        }

        if (ctx.fun->hasLocal(identExpr->name))
        {
            auto localIdx = ctx.fun->getLocalIdx(identExpr->name);
            genExpr(ctx, rhsExpr);
            ctx.addStr("op:'dup', idx:0");
            ctx.addStr("op:'set_local', idx:" + std::to_string(localIdx));
        }
        else
        {
            genExpr(ctx, rhsExpr);
            ctx.addStr("op:'push', val:@global_obj");
            ctx.addStr("op:'push', val:'" + identExpr->name + "'");
            ctx.addStr("op:'dup', idx:2");
            ctx.addOp("set_field");
        }

        return;
    }

    // Assignment to a property or array element
    if (auto binOp = dynamic_cast<BinOpExpr*>(lhsExpr))
    {
        // Object properties
        if (binOp->op == &OP_MEMBER)
        {
            // Get the field name
            auto identExpr = dynamic_cast<IdentExpr*>(binOp->rhsExpr);
            assert (identExpr);

            // Evaluate the rhs value
            genExpr(ctx, rhsExpr);

            // Evaluate the object/base
            genExpr(ctx, binOp->lhsExpr);

            ctx.addStr("op:'push', val:'" + identExpr->name + "'");
            ctx.addStr("op:'dup', idx:2");
            ctx.addOp("set_field");

            return;
        }
        // Array elements
        else if (binOp->op == &OP_INDEX)
        {
            // Evaluate the array
            genExpr(ctx, binOp->lhsExpr);

            // Evaluate the index
            genExpr(ctx, binOp->rhsExpr);

            // Evaluate the rhs value
            genExpr(ctx, rhsExpr);

            //ctx.addStr("op:'dup', idx:2");

            //ctx.addOp("set_elem");
            runtimeCall(ctx, "setElem", 3);
            return;
        }

        assert (false);
    }

    assert (false);
}
