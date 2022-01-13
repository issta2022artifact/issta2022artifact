#include "VFPExtractor.h"
#include <fstream>

using namespace SVF;
using namespace SVFUtil;
using namespace llvm;
using namespace z3;

/*!
 * initialize vul api set to support slicing
 */
void VFPExtractor::initVulAPI(const std::string apiPath)
{
    SVFUtil::outs() << "Initializing key point set with " << apiPath << "...\n";
    std::ifstream iFile(apiPath.c_str());
    assert(iFile.good() && "API set path not exists!");
    std::string apis;
    iFile >> apis;

    std::string::size_type pos1, pos2;
    std::string::size_type len = apis.length();
    pos2 = apis.find(",");
    pos1 = 0;
    while (std::string::npos != pos2)
    {
        _vulAPIS.insert(apis.substr(pos1, pos2 - pos1));

        pos1 = pos2 + 1;
        pos2 = apis.find(",", pos1);
    }
    if (pos1 != len)
        _vulAPIS.insert(apis.substr(pos1));

    SVFUtil::outs() << "API set size: " << std::to_string(_vulAPIS.size()) << "\n";
}

/*!
 * whether the svfgNode is a function exit node
 * principle: all of its successors are out-of-method
 */
bool VFPExtractor::isMethodExit(const SVFGNode *svfgNode, std::string methodName, std::string fileName)
{

    for (auto edge : svfgNode->getOutEdges())
    {
        const SVFGNode *dstNode = edge->getDstNode();
        const SVFFunction *dstFunc = dstNode->getFun();
        if (getSVFGFileName(dstNode) == "")
            return false;
        // TODO: is it possible that dstNode does not have fileName but the next node has fileName?
        if (dstFunc && dstFunc->getName().str() == methodName && getSVFGFileName(dstNode) == fileName)
            return false;
    }
    return true;
}

/*!
 * Get the meta data (line number and file name) info of a LLVM value
 */
u32_t VFPExtractor::getLineNumber(const Value *val)
{
    if (val == nullptr)
        return 0;

    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(val))
    {
        if (SVFUtil::isa<AllocaInst>(inst))
        {
            for (llvm::DbgInfoIntrinsic *DII : FindDbgAddrUses(const_cast<Instruction *>(inst)))
            {
                if (llvm::DbgDeclareInst *DDI = SVFUtil::dyn_cast<llvm::DbgDeclareInst>(DII))
                {
                    llvm::DIVariable *DIVar = SVFUtil::cast<llvm::DIVariable>(DDI->getVariable());
                    return DIVar->getLine();
                }
            }
        }
        else if (MDNode *N = inst->getMetadata("dbg")) // Here I is an LLVM instruction
        {
            llvm::DILocation *Loc = SVFUtil::cast<llvm::DILocation>(N); // DILocation is in DebugInfo.h
            unsigned Line = Loc->getLine();
            unsigned Column = Loc->getColumn();
            StringRef File = Loc->getFilename();
            //StringRef Dir = Loc.getDirectory();
            if (File.str().empty() || Line == 0)
            {
                auto inlineLoc = Loc->getInlinedAt();
                if (inlineLoc)
                {
                    Line = inlineLoc->getLine();
                    Column = inlineLoc->getColumn();
                    File = inlineLoc->getFilename();
                }
            }
            return Line;
        }
    }
    else if (const Argument *argument = SVFUtil::dyn_cast<Argument>(val))
    {
        /*
        * https://reviews.llvm.org/D18074?id=50385
        * looks like the relevant
        */
        if (llvm::DISubprogram *SP = argument->getParent()->getSubprogram())
        {
            if (SP->describes(argument->getParent()))
                return SP->getLine();
        }
    }
    else if (const GlobalVariable *gvar = SVFUtil::dyn_cast<GlobalVariable>(val))
    {
        NamedMDNode *CU_Nodes = gvar->getParent()->getNamedMetadata("llvm.dbg.cu");
        if (CU_Nodes)
        {
            for (unsigned i = 0, e = CU_Nodes->getNumOperands(); i != e; ++i)
            {
                llvm::DICompileUnit *CUNode = SVFUtil::cast<llvm::DICompileUnit>(CU_Nodes->getOperand(i));
                for (llvm::DIGlobalVariableExpression *GV : CUNode->getGlobalVariables())
                {
                    llvm::DIGlobalVariable *DGV = GV->getVariable();

                    if (DGV->getName() == gvar->getName())
                    {
                        return DGV->getLine();
                    }
                }
            }
        }
    }
    else if (const Function *func = SVFUtil::dyn_cast<Function>(val))
    {
        if (llvm::DISubprogram *SP = func->getSubprogram())
        {
            if (SP->describes(func))
                return SP->getLine();
        }
    }
    else if (const BasicBlock *bb = SVFUtil::dyn_cast<BasicBlock>(val))
    {
        return getLineNumber(bb->getFirstNonPHI());
    }
    else if (SVFUtil::isConstantData(val))
    {
        SVFUtil::outs() << "constant data";
    }
    else
    {
        SVFUtil::outs() << "Can only get source location for instruction, argument, global var, function or constant data.";
    }

    return 0;
}

/*!
 * Whether the line number is wrong
 */
bool VFPExtractor::isWrongLineNum(const SVFGNode *svfgNode)
{
    if (SVFUtil::dyn_cast<FormalOUTSVFGNode>(svfgNode))
    {
        return true;
    }
    return false;
}
/*!
 * Get the line number of a SVFG node
 */
u32_t VFPExtractor::getSVFGLineNumber(const SVFGNode *svfgNode)
{
    if (isWrongLineNum(svfgNode))
        return 0;
    return getLineNumber(getSVFGValue(svfgNode));
}

/*!
 * Get the meta data (file name) info of a LLVM value
 */
const std::string VFPExtractor::getFileName(const Value *val)
{
    if (val == nullptr)
        return "";

    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(val))
    {
        if (SVFUtil::isa<AllocaInst>(inst))
        {
            for (llvm::DbgInfoIntrinsic *DII : FindDbgAddrUses(const_cast<Instruction *>(inst)))
            {
                if (llvm::DbgDeclareInst *DDI = SVFUtil::dyn_cast<llvm::DbgDeclareInst>(DII))
                {
                    llvm::DIVariable *DIVar = SVFUtil::cast<llvm::DIVariable>(DDI->getVariable());
                    return DIVar->getFilename().str();
                }
            }
        }
        else if (MDNode *N = inst->getMetadata("dbg")) // Here I is an LLVM instruction
        {
            llvm::DILocation *Loc = SVFUtil::cast<llvm::DILocation>(N); // DILocation is in DebugInfo.h
            unsigned Line = Loc->getLine();
            unsigned Column = Loc->getColumn();
            StringRef File = Loc->getFilename();
            //StringRef Dir = Loc.getDirectory();
            if (File.str().empty() || Line == 0)
            {
                auto inlineLoc = Loc->getInlinedAt();
                if (inlineLoc)
                {
                    Line = inlineLoc->getLine();
                    Column = inlineLoc->getColumn();
                    File = inlineLoc->getFilename();
                }
            }
            return File.str();
        }
    }
    else if (const Argument *argument = SVFUtil::dyn_cast<Argument>(val))
    {
        /*
        * https://reviews.llvm.org/D18074?id=50385
        * looks like the relevant
        */
        if (llvm::DISubprogram *SP = argument->getParent()->getSubprogram())
        {
            if (SP->describes(argument->getParent()))
                return SP->getFilename().str();
        }
    }
    else if (const GlobalVariable *gvar = SVFUtil::dyn_cast<GlobalVariable>(val))
    {
        NamedMDNode *CU_Nodes = gvar->getParent()->getNamedMetadata("llvm.dbg.cu");
        if (CU_Nodes)
        {
            for (unsigned i = 0, e = CU_Nodes->getNumOperands(); i != e; ++i)
            {
                llvm::DICompileUnit *CUNode = SVFUtil::cast<llvm::DICompileUnit>(CU_Nodes->getOperand(i));
                for (llvm::DIGlobalVariableExpression *GV : CUNode->getGlobalVariables())
                {
                    llvm::DIGlobalVariable *DGV = GV->getVariable();

                    if (DGV->getName() == gvar->getName())
                    {
                        return DGV->getFilename().str();
                    }
                }
            }
        }
    }
    else if (const Function *func = SVFUtil::dyn_cast<Function>(val))
    {
        if (llvm::DISubprogram *SP = func->getSubprogram())
        {
            if (SP->describes(func))
                return SP->getFilename().str();
        }
    }
    else if (const BasicBlock *bb = SVFUtil::dyn_cast<BasicBlock>(val))
    {
        return getFileName(bb->getFirstNonPHI());
    }
    else if (SVFUtil::isConstantData(val))
    {
        SVFUtil::outs() << "constant data";
    }
    else
    {
        SVFUtil::outs() << "Can only get source location for instruction, argument, global var, function or constant data.";
    }

    return "";
}

/*! 
 * Get the value of a SVFG node
 */
const Value *VFPExtractor::getSVFGValue(const SVFGNode *svfgNode)
{
    const ICFGNode *node = svfgNode->getICFGNode();
    if (const IntraBlockNode *bNode = SVFUtil::dyn_cast<IntraBlockNode>(node))
    {
        return bNode->getInst();
    }
    else if (const CallBlockNode *cNode = SVFUtil::dyn_cast<CallBlockNode>(node))
    {
        return cNode->getCallSite();
    }
    else if (const RetBlockNode *rNode = SVFUtil::dyn_cast<RetBlockNode>(node))
    {
        return rNode->getCallSite();
    }
    else if (const FunExitBlockNode *fExNode = SVFUtil::dyn_cast<FunExitBlockNode>(node))
    {
        if (!SVFUtil::isExtCall(fExNode->getFun()))
            return SVFUtil::getFunExitBB(fExNode->getFun()->getLLVMFun())->getFirstNonPHI();
    }
    else if (const FunEntryBlockNode *fEnNode = SVFUtil::dyn_cast<FunEntryBlockNode>(node))
    {
        if (!SVFUtil::isExtCall(fEnNode->getFun()))
            return SVFUtil::getFunExitBB(fEnNode->getFun()->getLLVMFun())->getFirstNonPHI();
    }
    else if (const GlobalBlockNode *gNode = SVFUtil::dyn_cast<GlobalBlockNode>(node))
    {
        SVFUtil::outs() << "{ GlobalBlockNode ID: " << gNode->getId() << "} \n";
    }
    return nullptr;
}

/*!
 * Get the file name of a SVFG node
 */
const std::string VFPExtractor::getSVFGFileName(const SVFGNode *svfgNode)
{
    return getFileName(getSVFGValue(svfgNode));
}

/*!
 * Get the API call name of a call SVFG node
 */
std::vector<std::string> VFPExtractor::getAPI(const SVFGNode *svfgNode, SVFG *svfg)
{
    const ICFGNode *node = svfgNode->getICFGNode();
    std::vector<std::string> res;
    if (const CallBlockNode *cNode = SVFUtil::dyn_cast<CallBlockNode>(node))
    {
        SVF::PTACallGraph::FunctionSet callees;
        svfg->getPTA()->getPTACallGraph()->getCallees(cNode, callees);
        for (const SVFFunction *svfFunc : callees)
        {
            if (svfFunc && _vulAPIS.count(svfFunc->getName().str()))
                res.push_back(svfFunc->getName().str());
        }
    }
    return res;
}

void VFPExtractor::recordType(const SVFGNode *svfgNode, std::vector<s32_t> &types)
{
    const Value *svfgValue = getSVFGValue(svfgNode);
    if (svfgValue)
        types.push_back(svfgValue->getType()->getTypeID());
    // if (SVFUtil::dyn_cast<CmpVFGNode>(svfgNode))
    // {
    //     const PAGNode *pagNode = SVFUtil::dyn_cast<CmpVFGNode>(svfgNode)->getRes();
    //     if (pagNode->getNodeKind() != PAGNode::DummyValNode && pagNode->getNodeKind() != PAGNode::DummyObjNode)
    //     {
    //         if (pagNode->getId() != SYMTYPE::BlackHole && pagNode->getId() != SYMTYPE::ConstantObj)
    //         {
    //             if (pagNode->hasValue())
    //             {
    //                 const Value *svfgValue = svfgNode->getValue();
    //                 if (svfgValue)
    //                     types.push_back(svfgValue->getType()->getTypeID());
    //             }
    //         }
    //     }
    // }
    // else if (SVFUtil::dyn_cast<BinaryOPVFGNode>(svfgNode))
    // {
    //     const PAGNode *pagNode = SVFUtil::dyn_cast<BinaryOPVFGNode>(svfgNode)->getRes();
    //     if (pagNode->getNodeKind() != PAGNode::DummyValNode && pagNode->getNodeKind() != PAGNode::DummyObjNode)
    //     {
    //         if (pagNode->getId() != SYMTYPE::BlackHole && pagNode->getId() != SYMTYPE::ConstantObj)
    //         {
    //             if (pagNode->hasValue())
    //             {
    //                 const Value *svfgValue = svfgNode->getValue();
    //                 if (svfgValue)
    //                     types.push_back(svfgValue->getType()->getTypeID());
    //             }
    //         }
    //     }
    // }
    // else if (SVFUtil::dyn_cast<PHIVFGNode>(svfgNode))
    // {
    //     const PAGNode *pagNode = SVFUtil::dyn_cast<PHIVFGNode>(svfgNode)->getRes();
    //     if (pagNode->getNodeKind() != PAGNode::DummyValNode && pagNode->getNodeKind() != PAGNode::DummyObjNode)
    //     {
    //         if (pagNode->getId() != SYMTYPE::BlackHole && pagNode->getId() != SYMTYPE::ConstantObj)
    //         {
    //             if (pagNode->hasValue())
    //             {
    //                 const Value *svfgValue = svfgNode->getValue();
    //                 if (svfgValue)
    //                     types.push_back(svfgValue->getType()->getTypeID());
    //             }
    //         }
    //     }
    // }
    // else if (SVFUtil::dyn_cast<ArgumentVFGNode>(svfgNode))
    // {
    //     const PAGNode *pagNode = SVFUtil::dyn_cast<ArgumentVFGNode>(svfgNode)->getParam();
    //     if (pagNode->getNodeKind() != PAGNode::DummyValNode && pagNode->getNodeKind() != PAGNode::DummyObjNode)
    //     {
    //         if (pagNode->getId() != SYMTYPE::BlackHole && pagNode->getId() != SYMTYPE::ConstantObj)
    //         {
    //             if (pagNode->hasValue())
    //             {
    //                 const Value *svfgValue = svfgNode->getValue();
    //                 if (svfgValue)
    //                     types.push_back(svfgValue->getType()->getTypeID());
    //             }
    //         }
    //     }
    // }
    // else
    // {
    //     const Value *svfgValue = svfgNode->getValue();
    //     if (svfgValue)
    //         types.push_back(svfgValue->getType()->getTypeID());
    // }
}

/*!
 * intraprocedurally traverse SVFG to obtain value flow paths
 */
void VFPExtractor::intraTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::string methodName, std::string fileName, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res)
{
    if (visited.count(cur))
        return;
    visited.insert(cur);
    path.push_back(cur);

    if (isMethodExit(cur, methodName, fileName))
    {
        // std::unordered_set<u32_t> visited_s;
        std::vector<std::pair<u32_t, u32_t>> statements;
        std::vector<std::string> apis;
        std::vector<s32_t> types;
        std::vector<s32_t> vfgNodeKs;
        std::vector<u32_t> nodeIDs;
        for (const SVFGNode *svfgNode : path)
        {

            std::string fileName = getSVFGFileName(svfgNode);

            nodeIDs.push_back(svfgNode->getId());

            u32_t lineNum = getSVFGLineNumber(svfgNode);
            // avoid consecutive same line number
            // e.g. addr->store load->store
            if (lineNum != 0 && fileName != "")
            {
                u32_t fileIdx = _file_to_idx[fileName];
                if (statements.empty() || (statements.back().first == fileIdx && statements.back().second != lineNum))
                {
                    statements.emplace_back(0, lineNum);
                }
            }

            // avoid duplicate line number
            // if (lineNum != 0 && !visited_s.count(lineNum))
            // {
            //     statements.push_back(lineNum);
            //     visited_s.insert(lineNum);
            // }

            std::vector<std::string> apiset = getAPI(svfgNode, svfg);
            for (const std::string api : apiset)
                apis.push_back(api);
            vfgNodeKs.push_back(svfgNode->getNodeKind());

            recordType(svfgNode, types);
        }

        VFlow *vflow = new VFlow();
        vflow->nodeIDs = nodeIDs;
        vflow->statements = statements;
        vflow->apis = apis;
        vflow->vfgNodeKinds = vfgNodeKs;
        vflow->types = types;
        res.push_back(vflow);
    }
    else
    {
        // cur is not method exit node
        for (auto edge : cur->getOutEdges())
        {
            const SVFGNode *dstNode = edge->getDstNode();
            const SVFFunction *srcFunc = dstNode->getFun();
            // traverse within method. When meeting nodes without fileName, we will continue traversal.
            if (getSVFGFileName(dstNode) == "" || (srcFunc && srcFunc->getName().str() == methodName && getSVFGFileName(dstNode) == fileName))
                intraTraverseSVFG(dstNode, svfg, methodName, fileName, visited, path, res);
        }
    }
    visited.erase(cur);
    path.pop_back();
    return;
}

/*!
 * backward traverse SVFG
 */
void VFPExtractor::backTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res, std::vector<std::pair<char, CallSiteID>> &callsites, std::vector<std::vector<std::pair<char, CallSiteID>>> &allCallsites)
{
    if (visited.count(cur))
        return;
    visited.insert(cur);
    path.push_back(cur);

    if (!cur->hasIncomingEdge())
    {
        std::vector<std::pair<u32_t, u32_t>> statements;
        // std::unordered_set<u32_t> visited_s;
        std::vector<std::string> apis;
        std::vector<s32_t> types;
        std::vector<s32_t> vfgNodeKs;
        std::vector<u32_t> nodeIDs;
        // discard the first node
        std::vector<const SVFGNode *> p(path.begin() + 1, path.end());

        for (const SVFGNode *svfgNode : p)
        {
            std::string fileName = getSVFGFileName(svfgNode);

            nodeIDs.push_back(svfgNode->getId());

            u32_t lineNum = getSVFGLineNumber(svfgNode);
            // if (lineNum != 0 && !visited_s.count(lineNum))
            // {
            //     visited_s.insert(lineNum);
            //     statements.push_back(lineNum);
            // }
            // avoid consecutive same line number
            // e.g. addr->store load->store
            if (lineNum != 0 && fileName != "")
            {
                u32_t fileIdx = _file_to_idx[fileName];
                if (statements.empty() || statements.back().first != fileIdx || (statements.back().first == fileIdx && statements.back().second != lineNum))
                {
                    statements.emplace_back(fileIdx, lineNum);
                }
            }

            std::vector<std::string> apiset = getAPI(svfgNode, svfg);
            for (const std::string api : apiset)
                apis.push_back(api);

            recordType(svfgNode, types);
            vfgNodeKs.push_back(svfgNode->getNodeKind());
        }
        if (!p.empty())
        {
            std::reverse(statements.begin(), statements.end());
            std::reverse(nodeIDs.begin(), nodeIDs.end());
            std::reverse(apis.begin(), apis.end());
            std::reverse(vfgNodeKs.begin(), vfgNodeKs.end());
            std::reverse(types.begin(), types.end());
            std::reverse(callsites.begin(), callsites.end());

            VFlow *vflow = new VFlow();
            vflow->nodeIDs = nodeIDs;
            vflow->statements = statements;
            vflow->apis = apis;
            vflow->vfgNodeKinds = vfgNodeKs;
            vflow->types = types;
            res.push_back(vflow);
            allCallsites.push_back(callsites);
        }
    }
    else
    {
        for (auto edge : cur->getInEdges())
        {
            const SVFGNode *srcNode = edge->getSrcNode();
            // const SVFFunction *srcFunc = srcNode->getFun();
            const CallDirSVFGEdge *dirCallEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(edge);
            const CallIndSVFGEdge *indCallEdge = SVFUtil::dyn_cast<CallIndSVFGEdge>(edge);
            const RetDirSVFGEdge *dirRetEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(edge);
            const RetIndSVFGEdge *indRetEdge = SVFUtil::dyn_cast<RetIndSVFGEdge>(edge);
            bool isCall = true;
            if (dirCallEdge)
            {
                callsites.emplace_back('(', dirCallEdge->getCallSiteId());
            }
            else if (indCallEdge)
            {
                callsites.emplace_back('(', indCallEdge->getCallSiteId());
            }
            else if (dirRetEdge)
            {
                callsites.emplace_back(')', dirRetEdge->getCallSiteId());
            }
            else if (indRetEdge)
            {
                callsites.emplace_back(')', indRetEdge->getCallSiteId());
            }
            else
            {
                // intra edge
                isCall = false;
            }

            backTraverseSVFG(srcNode, svfg, visited, path, res, callsites, allCallsites);
            if (isCall)
                callsites.pop_back();
        }
    }
    visited.erase(cur);
    path.pop_back();
    return;
}

/*!
 * forward traverse SVFG
 */
void VFPExtractor::forTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res, std::vector<std::pair<char, CallSiteID>> &callsites, std::vector<std::vector<std::pair<char, CallSiteID>>> &allCallsites)
{
    if (visited.count(cur))
        return;
    visited.insert(cur);
    path.push_back(cur);

    if (!cur->hasOutgoingEdge())
    {
        std::vector<std::pair<u32_t, u32_t>> statements;
        // std::unordered_set<u32_t> visited_s;
        std::vector<std::string> apis;
        std::vector<s32_t> types;
        std::vector<s32_t> vfgNodeKs;
        std::vector<u32_t> nodeIDs;
        for (const SVFGNode *svfgNode : path)
        {
            nodeIDs.push_back(svfgNode->getId());
            std::string fileName = getSVFGFileName(svfgNode);
            nodeIDs.push_back(svfgNode->getId());

            u32_t lineNum = getSVFGLineNumber(svfgNode);
            // if (lineNum != 0 && !visited_s.count(lineNum))
            // {
            //     visited_s.insert(lineNum);
            //     statements.push_back(lineNum);
            // }
            // avoid consecutive same line number
            // e.g. addr->store load->store
            if (lineNum != 0 && fileName != "")
            {
                u32_t fileIdx = _file_to_idx[fileName];
                if (statements.empty() || statements.back().first != fileIdx || (statements.back().first == fileIdx && statements.back().second != lineNum))
                {
                    statements.emplace_back(fileIdx, lineNum);
                }
            }

            std::vector<std::string> apiset = getAPI(svfgNode, svfg);
            for (const std::string api : apiset)
                apis.push_back(api);

            recordType(svfgNode, types);
            vfgNodeKs.push_back(svfgNode->getNodeKind());
        }

        VFlow *vflow = new VFlow();
        vflow->nodeIDs = nodeIDs;
        vflow->statements = statements;
        vflow->apis = apis;
        vflow->vfgNodeKinds = vfgNodeKs;
        vflow->types = types;
        res.push_back(vflow);
        allCallsites.push_back(callsites);
    }
    else
    {
        for (auto edge : cur->getOutEdges())
        {
            const SVFGNode *dstNode = edge->getDstNode();
            // const SVFFunction *dstFunc = dstNode->getFun();
            const CallDirSVFGEdge *dirCallEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(edge);
            const CallIndSVFGEdge *indCallEdge = SVFUtil::dyn_cast<CallIndSVFGEdge>(edge);
            const RetDirSVFGEdge *dirRetEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(edge);
            const RetIndSVFGEdge *indRetEdge = SVFUtil::dyn_cast<RetIndSVFGEdge>(edge);
            bool isCall = true;
            if (dirCallEdge)
            {
                callsites.emplace_back('(', dirCallEdge->getCallSiteId());
            }
            else if (indCallEdge)
            {
                callsites.emplace_back('(', indCallEdge->getCallSiteId());
            }
            else if (dirRetEdge)
            {
                callsites.emplace_back(')', dirRetEdge->getCallSiteId());
            }
            else if (indRetEdge)
            {
                callsites.emplace_back(')', indRetEdge->getCallSiteId());
            }
            else
            {
                // intra edge
                isCall = false;
            }

            forTraverseSVFG(dstNode, svfg, visited, path, res, callsites, allCallsites);
            if (isCall)
                callsites.pop_back();
        }
    }
    visited.erase(cur);
    path.pop_back();
    return;
}

/*!
 * whether the target callsites satisfy cfl
 */
bool VFPExtractor::satisfyCFL(std::vector<std::pair<char, CallSiteID>> &callsites)
{
    std::stack<CallSiteID> st;
    for (auto &callsite : callsites)
    {
        if (callsite.first == '(')
        {
            st.push(callsite.second);
        }
        else if (callsite.first == ')')
        {
            if (st.empty())
                return false;
            else
            {
                CallSiteID tp = st.top();
                st.pop();
                if (tp != callsite.second)
                    return false;
            }
        }
    }

    return true;
}
/*!
 * merge forward and backward vfps using cfl
 */
void VFPExtractor::mergeVFPs(std::vector<const VFlow *> &bk_res, std::vector<std::vector<std::pair<char, CallSiteID>>> &bk_allCallsites, std::vector<const VFlow *> &f_res, std::vector<std::vector<std::pair<char, CallSiteID>>> &f_allCallsites, const SVFGNode *src, SVFG *svfg)
{
    if (bk_res.empty() && f_res.empty())
    {
        return;
    }
    else if (bk_res.empty())
    {
        u32_t i = 0;
        for (auto I = f_res.begin(), E = f_res.end(); I != E; ++I)
        {
            if (satisfyCFL(f_allCallsites[i]))
            {
                _vfps.push_back(f_res[i]);
            }
            else
            {
                delete (*I);
            }
            ++i;
        }
    }
    else if (f_res.empty())
    {
        u32_t i = 0;
        std::string fileName = getSVFGFileName(src);
        if (fileName == "")
        {
            for (auto I = bk_res.begin(), E = bk_res.end(); I != E; ++I)
            {
                if (satisfyCFL(bk_allCallsites[i]))
                {
                    _vfps.push_back(bk_res[i]);
                }
                else
                {
                    delete (*I);
                }
                ++i;
            }
        }
        else
        {
            u32_t fileIdx = _file_to_idx[fileName];
            for (auto I = bk_res.begin(), E = bk_res.end(); I != E; ++I)
            {
                if (satisfyCFL(bk_allCallsites[i]))
                {
                    VFlow *vf_n = new VFlow();
                    vf_n->apis = (*I)->apis;
                    vf_n->nodeIDs = (*I)->nodeIDs;
                    vf_n->statements = (*I)->statements;
                    vf_n->types = (*I)->types;
                    vf_n->vfgNodeKinds = (*I)->vfgNodeKinds;

                    vf_n->nodeIDs.push_back(src->getId());

                    u32_t lineNum = getSVFGLineNumber(src);

                    if (lineNum != 0)
                    {
                        // avoid consecutive same line number
                        if (vf_n->statements.empty() || vf_n->statements.back().first != fileIdx || (vf_n->statements.back().first == fileIdx && vf_n->statements.back().second != lineNum))
                            vf_n->statements.emplace_back(fileIdx, lineNum);
                    }

                    std::vector<std::string> apiset = getAPI(src, svfg);
                    for (const std::string api : apiset)
                        vf_n->apis.push_back(api);

                    recordType(src, vf_n->types);
                    vf_n->vfgNodeKinds.push_back(src->getNodeKind());
                    _vfps.push_back(vf_n);
                }
                delete (*I);
                ++i;
            }
        }
    }
    else
    {
        for (u32_t i = 0; i < f_res.size(); i++)
        {
            for (u32_t j = 0; j < bk_res.size(); j++)
            {
                std::vector<std::pair<char, CallSiteID>> intactCallsites(bk_allCallsites[j].begin(), bk_allCallsites[j].end());

                intactCallsites.reserve(intactCallsites.size() + std::distance(f_allCallsites[i].begin(), f_allCallsites[i].end()));
                intactCallsites.insert(intactCallsites.end(), f_allCallsites[i].begin(), f_allCallsites[i].end());
                if (satisfyCFL(intactCallsites))
                {

                    const VFlow *f_vf = f_res[i];
                    const VFlow *b_vf = bk_res[j];
                    VFlow *vf_n = new VFlow();
                    vf_n->apis = b_vf->apis;
                    vf_n->apis.reserve(vf_n->apis.size() + std::distance(f_vf->apis.begin(), f_vf->apis.end()));
                    vf_n->apis.insert(vf_n->apis.end(), f_vf->apis.begin(), f_vf->apis.end());

                    vf_n->statements = b_vf->statements;
                    if (!f_vf->statements.empty())
                    {
                        if (vf_n->statements.empty())
                        {
                            vf_n->statements = f_vf->statements;
                        }
                        else
                        {
                            if (f_vf->statements.front() != vf_n->statements.back())
                            {
                                vf_n->statements.reserve(vf_n->statements.size() + std::distance(f_vf->statements.begin(), f_vf->statements.end()));
                                vf_n->statements.insert(vf_n->statements.end(), f_vf->statements.begin(), f_vf->statements.end());
                            }
                            // avoid consecutive same line number
                            else
                            {
                                vf_n->statements.reserve(vf_n->statements.size() + std::distance(f_vf->statements.begin() + 1, f_vf->statements.end()));
                                vf_n->statements.insert(vf_n->statements.end(), f_vf->statements.begin() + 1, f_vf->statements.end());
                            }
                        }
                    }

                    vf_n->types = b_vf->types;
                    vf_n->types.reserve(vf_n->types.size() + std::distance(f_vf->types.begin(), f_vf->types.end()));
                    vf_n->types.insert(vf_n->types.end(), f_vf->types.begin(), f_vf->types.end());
                    vf_n->nodeIDs = b_vf->nodeIDs;
                    vf_n->nodeIDs.reserve(vf_n->nodeIDs.size() + std::distance(f_vf->nodeIDs.begin(), f_vf->nodeIDs.end()));
                    vf_n->nodeIDs.insert(vf_n->nodeIDs.end(), f_vf->nodeIDs.begin(), f_vf->nodeIDs.end());
                    vf_n->vfgNodeKinds = b_vf->vfgNodeKinds;
                    vf_n->vfgNodeKinds.reserve(vf_n->vfgNodeKinds.size() + std::distance(f_vf->vfgNodeKinds.begin(), f_vf->vfgNodeKinds.end()));
                    vf_n->vfgNodeKinds.insert(vf_n->vfgNodeKinds.end(), f_vf->vfgNodeKinds.begin(), f_vf->vfgNodeKinds.end());
                    _vfps.push_back(vf_n);
                }
            }
        }
        for (auto I = bk_res.begin(), E = bk_res.end(); I != E; ++I)
            delete (*I);
        for (auto I = f_res.begin(), E = f_res.end(); I != E; ++I)
            delete (*I);
    }
    return;
}
/*!
 * extract slice-level value flow paths
 */
void VFPExtractor::extraceSliceVFPs(SVFG *svfg, const std::string apiPath, const std::string OUTPUT)
{
    initVulAPI(apiPath);
    SVFUtil::outs() << "Extracting slice-level value flow paths...\n";
    if (!_vfps.empty())
    {
        SVFUtil::outs() << "Not empty value flow paths!\n";
        _vfps.clear();
        _file_to_idx.clear();
        _files.clear();
    }

    std::unordered_set<const SVFGNode *> srcs;
    for (auto it = svfg->begin(); it != svfg->end(); ++it)
    {
        const SVFGNode *svfgNode = it->second;
        // init files
        std::string fileName = getSVFGFileName(svfgNode);
        if (fileName != "")
        {
            if (!_file_to_idx.count(fileName))
            {
                _files.push_back(fileName);
                _file_to_idx[fileName] = _files.size() - 1;
            }
        }
        std::vector<std::string> apis = getAPI(svfgNode, svfg);
        for (std::string api : apis)
        {
            if (_vulAPIS.count(api))
            {
                srcs.insert(svfgNode);
                break;
            }
        }
    }
    std::unordered_set<const SVFGNode *> visited;
    std::unordered_set<std::string> visited_vf;
    std::vector<const SVFGNode *> path;
    std::vector<std::pair<char, CallSiteID>> callsites;
    SVFUtil::outs() << "Total " << std::to_string(srcs.size()) << " slicing points\n";
    int ct = 0;
    outputFiles(OUTPUT + "files.tmp");
    for (const SVFGNode *src : srcs)
    {
        _vfps.clear();
        SVFUtil::outs() << getSVFGFileName(src) << "@" << std::to_string(getSVFGLineNumber(src)) << ": ";
        std::vector<const VFlow *> bk_res;
        std::vector<std::vector<std::pair<char, CallSiteID>>> bk_allCallsites;

        backTraverseSVFG(src, svfg, visited, path, bk_res, callsites, bk_allCallsites);

        std::vector<const VFlow *> f_res;
        std::vector<std::vector<std::pair<char, CallSiteID>>> f_allCallsites;
        forTraverseSVFG(src, svfg, visited, path, f_res, callsites, f_allCallsites);

        mergeVFPs(bk_res, bk_allCallsites, f_res, f_allCallsites, src, svfg);
        SVFUtil::outs() << std::to_string(_vfps.size()) << " VFPs\n";
        dump(OUTPUT + "outs/" + std::to_string(ct) + ".output.tmp");
        ++ct;
    }

    return;
}

/*!
 * extract method-level value flow paths
 */
void VFPExtractor::extractMethodVFPs(SVFG *svfg, std::string methodName, std::string apiPath, const std::string fileName, const std::string OUTPUT)
{
    initVulAPI(apiPath);
    if (!_vfps.empty())
    {
        SVFUtil::outs() << "Not empty value flow paths!\n";
        _vfps.clear();
        _file_to_idx.clear();
        _files.clear();
    }
    _file_to_idx[fileName] = 0;
    _files.push_back(fileName);
    SVFUtil::outs() << "Extracting method-level value flow paths...\n";

    std::unordered_set<const SVFGNode *> visited;
    std::vector<const SVFGNode *> path;

    std::unordered_set<const SVFGNode *> srcs;
    for (auto it = svfg->begin(); it != svfg->end(); ++it)
    {
        const SVFGNode *svfgNode = it->second;
        const SVFFunction *svfFunc = svfgNode->getFun();
        if (svfFunc && svfFunc->getName().str() == methodName && getSVFGFileName(svfgNode) == fileName)
        {
            bool isEntry = true;
            for (auto edge : svfgNode->getInEdges())
            {
                const SVFFunction *srcFunc = edge->getSrcNode()->getFun();
                if (srcFunc && srcFunc->getName().str() == methodName && getSVFGFileName(svfgNode) == fileName)
                {
                    isEntry = false;
                    break;
                }
            }
            if (isEntry)
                srcs.insert(svfgNode);
        }
    }

    for (const SVFGNode *src : srcs)
        intraTraverseSVFG(src, svfg, methodName, fileName, visited, path, _vfps);
    SVFUtil::outs() << "End extracting, total: " << std::to_string(_vfps.size()) << " VFPs\n";
    outputFiles(OUTPUT + "files.tmp");
    dump(OUTPUT + "output.tmp");

    return;
}

/*!
 * output one value flow path
 */
void VFPExtractor::outputVFP(llvm::raw_ostream &os, const VFlow *vfp)
{
    // std::string s_files;
    // for (const std::string &f : _files)
    // {
    //     s_files = s_files + f + "|";
    // }
    std::string s_statements;
    // file_name , line number |
    for (std::pair<u32_t, u32_t> statement : vfp->statements)
    {
        s_statements = s_statements + std::to_string(statement.first) + "," + std::to_string(statement.second) + "|";
    }
    std::string s_apis;
    for (std::string api : vfp->apis)
    {
        s_apis = s_apis + api + "|";
    }
    std::string s_types;
    for (s32_t type : vfp->types)
    {
        s_types = s_types + std::to_string(type) + "|";
    }
    std::string s_vfgNodeKinds;
    for (s32_t vfgNodeKind : vfp->vfgNodeKinds)
    {
        s_vfgNodeKinds = s_vfgNodeKinds + std::to_string(vfgNodeKind) + "|";
    }
    std::string s_nodeIDs;
    for (u32_t nodeID : vfp->nodeIDs)
    {
        s_nodeIDs = s_nodeIDs + std::to_string(nodeID) + "|";
    }

    // os << s_files << "\n";
    os << s_statements << "\n";
    os << s_apis << "\n";
    os << s_types << "\n";
    os << s_vfgNodeKinds << "\n";
    os << s_nodeIDs << "\n";
}

/*!
 * output source code files
 */
void VFPExtractor::outputFiles(const string fileName)
{
    SVFUtil::outs() << "Writing source code files...\n";
    std::string s_files;
    for (const std::string &f : _files)
    {
        s_files = s_files + f + "|";
    }
    std::error_code errInfo;
    llvm::ToolOutputFile F(fileName.c_str(), errInfo, llvm::sys::fs::F_None);
    if (!errInfo)
    {
        // dump the source code files to fileName
        F.os() << s_files;
        F.os().close();
        if (!F.os().has_error())
        {
            SVFUtil::outs() << "\n";
            F.keep();
            SVFUtil::outs() << "======Source code files is saved! ======\nPathname: " << fileName << "\n";
            return;
        }
    }
    SVFUtil::outs() << "  error opening file for writing!\n";
    F.os().clear_error();
}

/*
 * traverse SVFG to get cfg node between svfgNode1 and svfgNode2
 */
void VFPExtractor::traverseSVFGNodePair(const SVFGNode *cur, const SVFGNode *svfgNode2, SVFG *svfg, std::unordered_set<const SVF::ICFGNode *> &svfgNodes, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path)
{
    if (visited.count(cur))
        return;
    path.push_back(cur);
    visited.insert(cur);

    // find target
    if (cur == svfgNode2)
    {
        for (auto &svfgNode : path)
        {
            svfgNodes.insert(svfgNode->getICFGNode());
        }
    }
    else
    {
        for (auto &edge : cur->getOutEdges())
        {
            traverseSVFGNodePair(edge->getDstNode(), svfgNode2, svfg, svfgNodes, visited, path);
        }
    }
    path.pop_back();
    visited.erase(cur);
}

/*
 * get cfg node between svfgNode1 and svfgNode2 on SVFG
 */
void VFPExtractor::getInBetweenSVFGNodes(const SVFGNode *svfgNode1, const SVFGNode *svfgNode2, SVFG *svfg, std::unordered_set<const SVF::ICFGNode *> &svfgNodes)
{
    std::unordered_set<const SVFGNode *> visited;
    std::vector<const SVFGNode *> path;
    traverseSVFGNodePair(svfgNode1, svfgNode2, svfg, svfgNodes, visited, path);
}
/*
 * whether there exists redefine between svfgNode1 and svfgNode2
 */
bool VFPExtractor::hasRedefine(SVFG *svfg, const SVFGNode *svfgNode1, const SVFGNode *svfgNode2, std::unordered_set<const SVF::ICFGNode *> &cfgPath)
{
    std::unordered_set<const SVF::ICFGNode *> svfgNodes;
    getInBetweenSVFGNodes(svfgNode1, svfgNode2, svfg, svfgNodes);
    if (cfgPath.count(svfgNode1->getICFGNode()))
        cfgPath.erase(svfgNode1->getICFGNode());
    if (cfgPath.count(svfgNode2->getICFGNode()))
        cfgPath.erase(svfgNode2->getICFGNode());
    if (svfgNodes.count(svfgNode1->getICFGNode()))
        svfgNodes.erase(svfgNode1->getICFGNode());
    if (svfgNodes.count(svfgNode2->getICFGNode()))
        svfgNodes.erase(svfgNode2->getICFGNode());
    for (const SVF::ICFGNode *icfgNode : cfgPath)
    {
        if (svfgNodes.count(icfgNode)) // has intersection
            return true;
    }
    return false;
}
/*!
 * sse checker
 * For two connected vfg node, we check the feasibility of all the cfg paths between them.
 * If one cfg path satisfy sse and there is no redefination in between, we say this vfg node pair is feasible.
 * Iff all vfg node pairs are feasible, the vfp is feasible.
 */
bool VFPExtractor::satisfySSE(const std::string &s_nodeIDs, SVFG *svfg, Traversal *traversal)
{
    if (s_nodeIDs.empty())
        return false;
    // discard the last "|"
    const std::string s_nodeIDs_tmp(s_nodeIDs.begin(), s_nodeIDs.end() - 1);

    std::vector<const ICFGNode *> icfgNodes;
    std::vector<const SVFGNode *> svfgNodes;
    std::string::size_type pos1, pos2;
    std::string::size_type len = s_nodeIDs_tmp.length();
    pos2 = s_nodeIDs_tmp.find("|");
    pos1 = 0;
    while (std::string::npos != pos2)
    {
        const SVFGNode *svfgNode = svfg->getSVFGNode(atoi(s_nodeIDs_tmp.substr(pos1, pos2 - pos1).c_str()));
        icfgNodes.push_back(svfgNode->getICFGNode());
        svfgNodes.push_back(svfgNode);
        pos1 = pos2 + 1;
        pos2 = s_nodeIDs_tmp.find("|", pos1);
    }
    if (pos1 != len)
    {
        const SVFGNode *svfgNode = svfg->getSVFGNode(atoi(s_nodeIDs_tmp.substr(pos1).c_str()));
        icfgNodes.push_back(svfgNode->getICFGNode());
        svfgNodes.push_back(svfgNode);
    }

    if (icfgNodes.size() == 1)
        return true;
    for (u32_t i = 0; i < icfgNodes.size() - 1; ++i)
    {
        std::vector<std::unordered_set<const SVF::ICFGNode *>> cfgPaths;

        if (!traversal->checkFeasibility(icfgNodes[i], icfgNodes[i + 1], cfgPaths))
        {
            return false;
        }
        else
        {
            // exists feasible cfg paths, check if all the paths have redefine.
            bool hasFeasiPath = false;
            for (std::unordered_set<const SVF::ICFGNode *> &cfgPath : cfgPaths)
            {
                // check if cfgPath is feasible (without redefine)
                if (!hasRedefine(svfg, svfgNodes[i], svfgNodes[i + 1], cfgPath))
                {
                    hasFeasiPath = true;
                    break;
                }
            }
            if (!hasFeasiPath)
                return false;
        }
    }
    return true;
}
/*!
 * filter vfps using sse checker
 */
void VFPExtractor::filterVFPs(SVFG *svfg, const std::string &output)
{
    const string inputVFP = output + "input.tmp";
    const string outputVFP = output + "output.tmp";
    SVFUtil::outs() << "Read input value flow paths: " << inputVFP << "...\n";
    std::ifstream iFile(inputVFP.c_str());
    assert(iFile.good() && "Input value flow paths not exists!");
    s32_t ct = 5;
    std::vector<std::string> buf;
    std::string outstr;
    std::error_code errInfo;
    llvm::ToolOutputFile F(outputVFP.c_str(), errInfo, llvm::sys::fs::F_None);
    std::string s;
    u32_t total = 0;
    ICFGECDetection *icfgECDetection = new ICFGECDetection(svfg->getPAG()->getICFG());
    Traversal *traversal = new Traversal(svfg->getPAG(), svfg->getPAG()->getICFG(), icfgECDetection);
    while (std::getline(iFile, s))
    {
        if (ct == 0)
        {
            ct = 5;
            std::string s_nodeIDs = buf[4];
            if (satisfySSE(s_nodeIDs, svfg, traversal))
            {
                ++total;
                outstr = outstr + buf[0] + "\n";
                outstr = outstr + buf[1] + "\n";
                outstr = outstr + buf[2] + "\n";
                outstr = outstr + buf[3] + "\n";
                outstr = outstr + buf[4] + "\n";
            }
            buf.clear();
        }
        buf.push_back(s);
        --ct;
    }
    delete traversal;
    delete icfgECDetection;
    if (!errInfo)
    {
        // dump the value flow path to output
        F.os() << outstr;
        F.os().close();
        if (!F.os().has_error())
        {
            SVFUtil::outs() << "\n";
            F.keep();
            SVFUtil::outs() << "total: " << std::to_string(total) << " VFPs\n";
            SVFUtil::outs() << "====== File is saved! ======\nPathname: " << outputVFP << "\n";
            return;
        }
    }
    SVFUtil::outs() << "  error opening file for writing!\n";
    F.os().clear_error();
    return;
}

/*!
 * dump value flow path to one tmp file
 */
void VFPExtractor::dump(const std::string &pname)
{
    SVFUtil::outs() << "Writing vfps to file...\n";
    std::error_code errInfo;
    llvm::ToolOutputFile F(pname.c_str(), errInfo, llvm::sys::fs::F_None);
    if (!errInfo)
    {
        // dump the value flow path to pname
        F.os() << *this;
        F.os().close();
        if (!F.os().has_error())
        {
            SVFUtil::outs() << "\n";
            F.keep();
            SVFUtil::outs() << "====== File is saved! ======\nPathname: " << pname << "\n";
            return;
        }
    }
    SVFUtil::outs() << "  error opening file for writing!\n";
    F.os().clear_error();
}