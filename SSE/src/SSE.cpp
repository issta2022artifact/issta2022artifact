#include "SSE.h"

using namespace SVF;
using namespace SVFUtil;
using namespace llvm;
using namespace z3;

static llvm::cl::opt<std::string> InputFilename(cl::Positional,
                                                llvm::cl::desc("<input bitcode>"), llvm::cl::init("-"));

// DFS from identified source node to dst node
void Traversal::dfs(const ICFGNode *src, const ICFGNode *dst)
{
    typedef std::pair<const ICFGNode *, int> Order;
    std::stack<Order> worklist;
    std::vector<const ICFGNode *> path;
    std::set<const ICFGNode *> visited;
    std::stack<const Instruction *> callstack;
    worklist.push(std::make_pair(src, 0));
    while (!worklist.empty())
    {
        Order cur = worklist.top();
        worklist.pop();
        while (!path.empty() && (cur.second < path.size()))
        {
            const ICFGNode *last = path.back();
            path.pop_back();
            visited.erase(last);
        }
        int index = cur.second + 1;
        if (visited.find(cur.first) == visited.end())
        {
            path.push_back(cur.first);
            visited.insert(cur.first);
            if (cur.first == dst)
            {
                paths.insert(path);
            }
            for (const ICFGEdge *edge : cur.first->getOutEdges())
            {
                if (edge->isIntraCFGEdge())
                {
                    worklist.push(std::make_pair(edge->getDstNode(), index));
                }
                else if (const CallCFGEdge *callEdge = SVFUtil::dyn_cast<CallCFGEdge>(edge))
                {
                    callstack.push(callEdge->getCallSite());
                    worklist.push(std::make_pair(edge->getDstNode(), index));
                }
                else if (const RetCFGEdge *retEdge = SVFUtil::dyn_cast<RetCFGEdge>(edge))
                { //edge->isRetCFGEdge()
                    if (callstack.top() == retEdge->getCallSite())
                    {
                        callstack.pop();
                        worklist.push(std::make_pair(edge->getDstNode(), index));
                    }
                }
            }
        }
    }
}

/// find valid circle for context sensitivity
void Traversal::findValidCircle()
{
    for (int i = 0; i < _icfgecDetection->getAllCycles().size(); ++i)
    {
        // TODO: context sensitivity checker
        _validCircle.insert(i);
    }
}

/// add loops which current icfg path passes
void Traversal::addLoopToPath(std::vector<const ICFGNode *> &path)
{
    std::unordered_set<const ICFGNode *> pathSet(path.begin(), path.end());
    std::unordered_set<int> toAddCircleIndex;
    for (const ICFGNode *icfgNode : path)
    {
        NodeID icfgNodeID = icfgNode->getId();
        if (_icfgecDetection->isInCycle(icfgNodeID))
        {
            for (int eCircleIndex : _icfgecDetection->getNodeIDCycleIndexBs(icfgNodeID))
            {
                if (_validCircle.count(eCircleIndex))
                    toAddCircleIndex.insert(eCircleIndex);
            }
        }
    }
    for (int idx : toAddCircleIndex)
    {
        for (NodeID nodeId : _icfgecDetection->getAllCycles()[idx])
        {
            if (pathSet.count(icfg->getICFGNode(nodeId)))
                continue;
            int i = _k;
            while (i--)
            {
                path.push_back(icfg->getICFGNode(nodeId));
            }
        }
    }
}

/// program entry
void Traversal::analyse()
{
    std::set<const ICFGNode *> start;
    std::set<const ICFGNode *> end;
    for (const ICFGNode *src : identifyStart(start))
    {
        for (const ICFGNode *dst : identifyEnd(end))
        {
            dfs(src, dst);
        }
    }
    context ctx;
    SSE *sse = new SSE(pag, ctx);
    for (vector<const ICFGNode *> path : paths)
    {
        addLoopToPath(path);
        SVFUtil::outs() << "\n *** start a path *** \n";
        for (const ICFGNode *node : path)
        {
            SVFUtil::outs() << node->getId() << "->";
        }
        SVFUtil::outs() << "\n *** finish a path *** \n";
        sse->translate(path);
    }
    delete sse;
}

bool Traversal::checkFeasibility(const ICFGNode *src, const ICFGNode *dst, std::vector<std::unordered_set<const ICFGNode *>> &cfgPaths)
{
    paths.clear();
    dfs(src, dst);
    bool isFeasi = false;
    context ctx;
    SSE *sse = new SSE(pag, ctx);
    for (vector<const ICFGNode *> path : paths)
    {
        addLoopToPath(path);
        if (sse->isFeasible(path))
        {
            isFeasi = true;
            cfgPaths.emplace_back(path.begin(), path.end());
        }
    }
    delete sse;
    return isFeasi;
}

bool SSE::isFeasible(vector<const ICFGNode *> &path)
{
    for (u32_t i = 0; i < path.size(); ++i)
    {
        const ICFGNode *iNode = path[i];
        if (const auto *ibNode = SVFUtil::dyn_cast<IntraBlockNode>(iNode))
        {
            /// Each IntraBlockNode contains multiple PAGEdges represents following data dependence
            for (const PAGEdge *pagE : ibNode->getPAGEdges())
            {
                if (pagE->getSrcNode()->isPointer() && pagE->getDstNode()->isPointer())
                {
                    /// pointer analysis has finished all pts
                    continue;
                }
                else
                {
                    if (const AddrPE *addr = SVFUtil::dyn_cast<AddrPE>(pagE))
                    {
                        translateAddr(addr);
                    }
                    else if (const CopyPE *copy = SVFUtil::dyn_cast<CopyPE>(pagE))
                    {
                        translateCopy(copy);
                    }
                    else if (const StorePE *store = SVFUtil::dyn_cast<StorePE>(pagE))
                    {
                        translateStore(store);
                    }
                    else if (const LoadPE *load = SVFUtil::dyn_cast<LoadPE>(pagE))
                    {
                        translateLoad(load);
                    }
                    else if (const GepPE *gep = SVFUtil::dyn_cast<GepPE>(pagE))
                    {
                        translateGep(gep);
                    }
                    else if (const BinaryOPPE *binary = SVFUtil::dyn_cast<BinaryOPPE>(pagE))
                    {
                        translateBinary(binary);
                        /// only need to process once
                        break;
                    }
                    else if (const CmpPE *cmp = SVFUtil::dyn_cast<CmpPE>(pagE))
                    {
                        translateCompare(cmp);
                        /// only need to process once
                        break;
                    }
                    else if (const UnaryOPPE *uppe = SVFUtil::dyn_cast<UnaryOPPE>(pagE))
                    {
                        if (const BranchInst *brInst = SVFUtil::dyn_cast<BranchInst>(uppe->getInst()))
                        {
                            /// only need to process conditional branch instruction : br i1 %cmp1, label %if.then, label %if.end
                            if (!brInst->isConditional())
                            {
                                break;
                            }
                        }
                        /// Unary node must have next instruction
                        assert(path.size() > i + 1);
                        translateUnary(uppe, path.at(i + 1));
                    }
                    else
                    {
                        SVFUtil::outs() << pagE->toString();
                        assert(false && " operand is not included!");
                    }
                }
            }
        }
        else
        {
            /// next node on ICFG path
            continue;
        }
    }
    /// one path finished, check and clean all symbolic values in the constraint table
    z3::check_result result = solver.check();
    bool res = false;
    switch (result)
    {
    case z3::sat:
    {
        // model m = solver.get_model();
        // SVFUtil::outs() << "SAT ***fun:\n"
        //                 << solver.to_smt2() << "***  finish \n  ";
        res = true;
        break;
    }
    case z3::unknown:
        // SVFUtil::outs() << "UNKNOW";
        res = true;
        break;
    case z3::unsat:
        // SVFUtil::outs() << "UNSAT";
        break;
    default:
        assert(false && "other z3 checking result?");
    }
    solver.reset();
    return res;
}

void SSE::translate(vector<const ICFGNode *> &path)
{
    for (u32_t i = 0; i < path.size(); ++i)
    {
        const ICFGNode *iNode = path[i];
        if (const auto *ibNode = SVFUtil::dyn_cast<IntraBlockNode>(iNode))
        {
            /// Each IntraBlockNode contains multiple PAGEdges represents following data dependence
            for (const PAGEdge *pagE : ibNode->getPAGEdges())
            {
                if (pagE->getSrcNode()->isPointer() && pagE->getDstNode()->isPointer())
                {
                    /// pointer analysis has finished all pts
                    continue;
                }
                else
                {
                    if (const AddrPE *addr = SVFUtil::dyn_cast<AddrPE>(pagE))
                    {
                        translateAddr(addr);
                    }
                    else if (const CopyPE *copy = SVFUtil::dyn_cast<CopyPE>(pagE))
                    {
                        translateCopy(copy);
                    }
                    else if (const StorePE *store = SVFUtil::dyn_cast<StorePE>(pagE))
                    {
                        translateStore(store);
                    }
                    else if (const LoadPE *load = SVFUtil::dyn_cast<LoadPE>(pagE))
                    {
                        translateLoad(load);
                    }
                    else if (const GepPE *gep = SVFUtil::dyn_cast<GepPE>(pagE))
                    {
                        translateGep(gep);
                    }
                    else if (const BinaryOPPE *binary = SVFUtil::dyn_cast<BinaryOPPE>(pagE))
                    {
                        translateBinary(binary);
                        /// only need to process once
                        break;
                    }
                    else if (const CmpPE *cmp = SVFUtil::dyn_cast<CmpPE>(pagE))
                    {
                        translateCompare(cmp);
                        /// only need to process once
                        break;
                    }
                    else if (const UnaryOPPE *uppe = SVFUtil::dyn_cast<UnaryOPPE>(pagE))
                    {
                        if (const BranchInst *brInst = SVFUtil::dyn_cast<BranchInst>(uppe->getInst()))
                        {
                            /// only need to process conditional branch instruction : br i1 %cmp1, label %if.then, label %if.end
                            if (!brInst->isConditional())
                            {
                                break;
                            }
                        }
                        /// Unary node must have next instruction
                        assert(path.size() > i + 1);
                        translateUnary(uppe, path.at(i + 1));
                    }
                    else
                    {
                        SVFUtil::outs() << pagE->toString();
                        assert(false && " operand is not included!");
                    }
                }
            }
        }
        else
        {
            /// next node on ICFG path
            continue;
        }
    }
    /// one path finished, check and clean all symbolic values in the constraint table
    check();
    assertZ3();
    solver.reset();
}
void SSE::translateAddr(const AddrPE *addr)
{
    /// pointer analysis finised this step
}
void SSE::translateCopy(const CopyPE *copy)
{
    z3::expr src = ctx;
    z3::expr dst = ctx;
    src = id2expr(copy->getSrcID(), src);
    dst = id2expr(copy->getDstID(), dst);
    solver.add(src == dst);
}
void SSE::translateLoad(const LoadPE *load)
{
    z3::expr dst = ctx;
    dst = id2expr(load->getDstID(), dst);
    for (NodeID tgt : pta->getPts(load->getSrcID()))
    {
        z3::expr src = ctx;
        src = id2expr(tgt, src);
        solver.add(src == dst);
    }
}
void SSE::translateStore(const StorePE *store)
{
    z3::expr src = ctx;

    src = id2expr(store->getSrcID(), src);
    for (NodeID tgt : pta->getPts(store->getDstID()))
    {
        z3::expr dst = ctx;
        dst = id2expr(tgt, dst);
        solver.add(src == dst);
    }
}
void SSE::translateUnary(const UnaryOPPE *uppe, const ICFGNode *icfgNode)
{
    //    SVFUtil::outs() << "\n branch condition" << uppe->toString();
    z3::expr srcExpr = ctx;
    srcExpr = id2expr(uppe->getSrcID(), srcExpr);
    if (const BranchInst *brInst = SVFUtil::dyn_cast<BranchInst>(uppe->getInst()))
    {
        /// only need to process conditional branch instruction : br i1 %cmp1, label %if.then, label %if.end
        if (brInst->isConditional())
        /// else for single branch instruction:  label %while.cond  pass
        {
            for (const ICFGEdge *edge : icfgNode->getInEdges())
            {
                if (const IntraCFGEdge *intraCfgEdge = SVFUtil::dyn_cast<IntraCFGEdge>(edge))
                {
                    const IntraCFGEdge::BranchCondition &branchCond = intraCfgEdge->getBranchCondtion();
                    if (branchCond.second == 0)
                    {
                        solver.add(srcExpr == false);
                    }
                    else
                    {
                        solver.add(srcExpr == true);
                    }
                }
                else
                    continue;
            }
        }
    }
    else if (const SwitchInst *switchInst = SVFUtil::dyn_cast<SwitchInst>(uppe->getInst()))
    {
        for (const ICFGEdge *edge : icfgNode->getInEdges())
        {
            if (uppe->getICFGNode() == edge->getSrcNode())
            {
                if (const IntraCFGEdge *intraCfgEdge = SVFUtil::dyn_cast<IntraCFGEdge>(edge))
                {
                    const IntraCFGEdge::BranchCondition &branchCond = intraCfgEdge->getBranchCondtion();
                    NodeID index = branchCond.second;
                    /// switch default case
                    if (index == 0)
                    {
                        continue;
                    }
                    /// index * 2 is the constant  , index*2 + 1 is the label for basic block ()
                    else
                    {
                        assert((index * 2) <= switchInst->getNumOperands() && "out of bound in switch operands!");
                        const Value *value = switchInst->getOperand(index * 2);
                        const ConstantInt *constantInt = SVFUtil::dyn_cast<ConstantInt>(value);
                        assert(constantInt && "not a constant int value ??");
                        uint64_t concrete = constantInt->getZExtValue();
                        solver.add(ctx.real_val(concrete) == srcExpr);
                    }
                }
                else
                    continue;
            }
            else
                continue;
        }
    }
    else
        assert(false && "not branchInst or SwitchInst ?");
}
void SSE::translateGep(const GepPE *gep)
{
    /// pts has finished p = &a -> fld
}
void SSE::translateCompare(const CmpPE *cmp)
{
    //    SVFUtil::outs() << "\n src :  " << cmp->getSrcNode()->toString() <<  "\n dst "  << cmp->getDstNode()->toString() << "\n edge  " << cmp->toString() ;
    z3::expr dstExpr = ctx;
    dstExpr = id2expr(cmp->getDstID(), dstExpr);
    const PAGNode *node = cmp->getDstNode();
    const PAG::CmpPEList &cmpList = pag->getCmpPEs(node);
    /// for binary node, it should only have one pair (2 edges) to represent the compare operation
    assert(cmpList.size() == 2);
    z3::expr srcExpr1 = ctx;
    z3::expr srcExpr2 = ctx;
    srcExpr1 = id2expr(cmpList[0]->getSrcID(), srcExpr1);
    srcExpr2 = id2expr(cmpList[1]->getSrcID(), srcExpr2);
    const CmpInst *cmpi = SVFUtil::dyn_cast<CmpInst>(cmp->getInst());
    assert(cmpi);
    switch (cmpi->getPredicate())
    {
    case CmpInst::FCMP_FALSE:
        solver.add(implies(srcExpr1 == srcExpr2, false));
        break;
    case CmpInst::FCMP_OEQ:
        solver.add(srcExpr1 == srcExpr2);
        break;
    case CmpInst::ICMP_EQ:
        solver.add(srcExpr1 == srcExpr2);
        break;
    case CmpInst::ICMP_NE:
        solver.add(srcExpr1 != srcExpr2);
        break;
    case CmpInst::ICMP_UGT:
        solver.add(srcExpr1 > srcExpr2);
        break;
    case CmpInst::ICMP_UGE:
        solver.add(srcExpr1 >= srcExpr2);
        break;
    case CmpInst::ICMP_ULT:
        solver.add(srcExpr1 < srcExpr2);
        break;
    case CmpInst::ICMP_ULE:
        solver.add(srcExpr1 <= srcExpr2);
        break;
    case CmpInst::ICMP_SGT:
        solver.add(srcExpr1 > srcExpr2);
        break;
    case CmpInst::ICMP_SGE:
        solver.add(srcExpr1 >= srcExpr2);
        break;
    case CmpInst::ICMP_SLT:
        solver.add(srcExpr1 < srcExpr2);
        break;
    case CmpInst::ICMP_SLE:
        solver.add(srcExpr1 <= srcExpr2);
        break;
    default:
        assert(false && " type is not included!");
    }
}

/// All binary operations https://llvm.org/docs/LangRef.html?highlight=urem#binary-operations
void SSE::translateBinary(const BinaryOPPE *binary)
{
    z3::expr dstExpr = ctx;
    dstExpr = id2expr(binary->getDstID(), dstExpr);
    const PAGNode *node = binary->getDstNode();
    const PAG::BinaryOPList &opList = pag->getBinaryPEs(node);
    /// for binary node, it should only have one pair (2 edges) to represent the binary operation
    assert(opList.size() == 2);
    z3::expr srcExpr1 = ctx;
    z3::expr srcExpr2 = ctx;
    srcExpr1 = id2expr(opList[0]->getSrcID(), srcExpr1);
    srcExpr2 = id2expr(opList[1]->getSrcID(), srcExpr2);

    const BinaryOperator *bo = SVFUtil::dyn_cast<BinaryOperator>(binary->getInst());
    assert(bo->isBinaryOp());
    switch (bo->getOpcode())
    {
    case BinaryOperator::Add:
        solver.add(dstExpr == (srcExpr1 + srcExpr2));
        break;
    case BinaryOperator::FAdd:
        solver.add(dstExpr == (srcExpr1 + srcExpr2));
        break;
    case BinaryOperator::Sub:
        solver.add(dstExpr == (srcExpr1 - srcExpr2));
        break;
    case BinaryOperator::FSub:
        solver.add(dstExpr == (srcExpr1 - srcExpr2));
        break;
    case BinaryOperator::Mul:
        solver.add(dstExpr == (srcExpr1 * srcExpr2));
        break;
    case BinaryOperator::FMul:
        solver.add(dstExpr == (srcExpr1 * srcExpr2));
        break;
    case BinaryOperator::UDiv:
        solver.add(dstExpr == (srcExpr1 / srcExpr2));
        break;
    case BinaryOperator::SDiv:
        solver.add(dstExpr == (srcExpr1 / srcExpr2));
        break;
    case BinaryOperator::FDiv:
        solver.add(dstExpr == (srcExpr1 / srcExpr2));
        break;
    case BinaryOperator::URem:
        solver.add(dstExpr == (srcExpr1 % srcExpr2));
        break;
    case BinaryOperator::SRem:
        solver.add(dstExpr == (srcExpr1 % srcExpr2));
        break;
    case BinaryOperator::FRem:
        solver.add(dstExpr == (srcExpr1 % srcExpr2));
        break;
    default:
        assert(false && " type is not included!");
    }
}

/// Use NodeID to generate corresponding variable expression name
const char *SSE::ID2Name(NodeID n)
{
    std::string name = "v" + to_string(n);
    const char *c = strdup(name.c_str());
    return c;
}

void SSE::assertZ3()
{
    z3::check_result result = solver.check();
    switch (result)
    {
    case z3::sat:
    {
        model m = solver.get_model();
        SVFUtil::outs() << "SAT ***fun:\n"
                        << solver.to_smt2() << "***  finish \n  ";
        break;
    }
    case z3::unknown:
        SVFUtil::outs() << "UNKNOW";
        break;
    case z3::unsat:
        SVFUtil::outs() << "UNSAT";
        break;
    default:
        assert(false && "other z3 checking result?");
    }
}

/// for ctest check result: SAT UNSAT UNKOWN
void SSE::check()
{
    z3::check_result result = solver.check();
    SVFUtil::outs() << result;
}

/// PAGNode convert to variable expression in Z3 referred from llvm::Type::TypeID
/// https://llvm.org/doxygen/classllvm_1_1Type.html
z3::expr &SSE::id2expr(NodeID id, expr &e)
{
    if (SVFUtil::isa<SVF::DummyObjPN>(pag->getPAGNode(id)) or SVFUtil::isa<SVF::DummyValPN>(pag->getPAGNode(id)))
    {
        e = ctx.int_const(ID2Name(id));
        z3SymbolTable.emplace(id, e);
    }
    else
    {
        /// PAGNode the SVF::Value type
        const Type *t = pag->getPAGNode(id)->getValue()->getType();
        e = createExprhelper(id, t, e);
    }
    return e;
}
z3::expr &SSE::createExprhelper(const NodeID id, const Type *t, z3::expr &e)
{
    switch (t->getTypeID())
    {
    case Type::TypeID::HalfTyID:
        e = ctx.bv_const(ID2Name(id), 16);
        break;
    case Type::TypeID::BFloatTyID:
        e = ctx.bv_const(ID2Name(id), 16);
        break;
    case Type::TypeID::FloatTyID:
        e = ctx.bv_const(ID2Name(id), 32);
        break;
    case Type::TypeID::DoubleTyID:
        e = ctx.bv_const(ID2Name(id), 64);
        break;
    case Type::TypeID::X86_FP80TyID:
        e = ctx.bv_const(ID2Name(id), 80);
        break;
    case Type::TypeID::FP128TyID:
        e = ctx.bv_const(ID2Name(id), 128);
        break;
    case Type::TypeID::PPC_FP128TyID:
        e = ctx.bv_const(ID2Name(id), 128);
        break;
        /// llvm::void type has no size
        /// in Z3 we process as bool value for br p, inst1, inst2, this is used as Expr(p) : true / false
    case Type::TypeID::VoidTyID:
        e = ctx.bv_const(ID2Name(id), 1);
        break;
    case Type::TypeID::LabelTyID:
        assert(false && "label type should init in node2Expr");
    case Type::TypeID::MetadataTyID:
        assert(false && "LLVM MetaData should init in node2Expr");
    case Type::TypeID::X86_MMXTyID:
        assert(false && "X86_MMXTyID should init in node2Expr");
    case Type::TypeID::X86_AMXTyID:
        assert(false && "X86_AMXTyID should init in node2Expr");
    case Type::TypeID::TokenTyID:
        assert(false && "TokenTyID should init in node2Expr");
        /// APInt s = (SVFUtil::dyn_cast<SVF::IntegerType>(v))->getMask();  0xFF : 8 bit for integer
    case Type::TypeID::IntegerTyID:
    {
        e = ctx.int_const(ID2Name(id));
        const PAGNode *node = pag->getPAGNode(id);
        /// constant data cases in math
        if (node->isConstantData())
        {
            const Value *value = node->getValue();
            const ConstantInt *constantInt = SVFUtil::dyn_cast<ConstantInt>(value);
            assert(constantInt && "not a constant int value ??");
            uint64_t concrete = constantInt->getZExtValue();
            e = ctx.real_val(concrete);
        }
        break;
    }
    case Type::TypeID::FunctionTyID:
    {
        const FunctionType *ft = SVFUtil::cast<FunctionType>(t);
        return createExprhelper(id, ft->getReturnType(), e);
    }
    case Type::TypeID::PointerTyID:
    {
        /// We init the obj for pointer base object type e.g. ptr = i32*  -> expr(ptr.name, i32);
        const PointerType *ptr = SVFUtil::cast<PointerType>(t);
        return createExprhelper(id, ptr->getPointerElementType(), e);
    }
    case Type::TypeID::StructTyID:
    {
        e = ctx.bv_const(ID2Name(id), 64);
        break;
    }
    case Type::TypeID::ArrayTyID:
        assert(false && "ArrayTyID should init in node2Expr");
    case Type::TypeID::FixedVectorTyID:
        assert(false && "FixedVectorTyID should init in node2Expr");
    case Type::TypeID::ScalableVectorTyID:
        assert(false && "ScalableVectorTyID should init in node2Expr");
    default:
        assert(false && "other type instruction? ");
    }
    return e;
}



int main(int argc, char **argv)
{
    int arg_num = 0;
    char **arg_value = new char *[argc];
    std::vector<std::string> moduleNameVec;
    SVFUtil::processArguments(argc, argv, arg_num, arg_value, moduleNameVec);
    cl::ParseCommandLineOptions(arg_num, arg_value,
                                "Whole Program Points-to Analysis\n");

    SVFModule *svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(moduleNameVec);
    /// Build Program Assignment Graph (PAG)
    PAGBuilder builder;
    PAG *pag = builder.build(svfModule);
    /// ICFG
    ICFG *icfg = pag->getICFG();
    icfg->dump(svfModule->getModuleIdentifier() + ".icfg");

    ICFGECDetection *icfgECDetection = new ICFGECDetection(icfg);
    Traversal * traversal = new Traversal(pag, icfg, icfgECDetection);
    traversal->analyse();

    SVF::LLVMModuleSet::releaseLLVMModuleSet();
    SVF::PAG::releasePAG();
    delete traversal;
    return 0;
}

//int main() {
//    TestECG();
//    return 0;
//}
