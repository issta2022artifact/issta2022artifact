#ifndef SVF_EX_SSE_H
#define SVF_EX_SSE_H

#include "WPA/Andersen.h"
#include "SVF-FE/PAGBuilder.h"
#include "z3++.h"
#include "EC.h"

// static symbolic execution
namespace SVF
{
    typedef ECDetection<ICFG *> ICFGECDetection; // johnson's detection for ICFG
    class SSE
    {
    public:
        SSE(PAG *pag, z3::context &_ctx) : pag(pag), ctx(_ctx), solver(_ctx)
        {
            pta = AndersenWaveDiff::createAndersenWaveDiff(pag);
        }
        void translate(std::vector<const ICFGNode *> &path);

        static const char *ID2Name(NodeID n);

        z3::expr &id2expr(NodeID id, z3::expr &e);
        /// create z3 expr depending from value type
        z3::expr &createExprhelper(const NodeID id, const Type *t, z3::expr &e);

        /// translate varies in PAGEdge kind helper
        virtual void translateAddr(const AddrPE *addr);
        virtual void translateCopy(const CopyPE *copy);
        virtual void translateLoad(const LoadPE *load);
        virtual void translateStore(const StorePE *store);
        virtual void translateBinary(const BinaryOPPE *binary);
        virtual void translateCompare(const CmpPE *cmp);
        virtual void translateUnary(const UnaryOPPE *uppe, const ICFGNode *icfgNode);
        virtual void translateGep(const GepPE *gep);
        bool isFeasible(vector<const ICFGNode *>& path);

        void assertZ3();

        /// change bv const to int const if src and dst are in different type
        void bv2int(z3::expr &src, z3::expr &dst);

        ///z3::SAT UNSAT UNKNOWN
        void check();

    private:
        PAG *pag;
        Andersen *pta;
        z3::context &ctx;
        z3::solver solver;
        Map<NodeID, z3::expr &> z3SymbolTable;
    };

    class Traversal
    {
    public:
        Traversal(PAG *pag, ICFG *icfg, ICFGECDetection *icfgecDetection, int k = 3) : pag(pag), icfg(icfg), _icfgecDetection(icfgecDetection), _k(k)
        {
            _icfgecDetection->simpleCycle();
            findValidCircle();
        }

        inline void setK(int k)
        {
            _k = k;
        }

        inline const int getK() const
        {
            return _k;
        }

        /// identify start node on icfg, the start node does not have Incoming Edges.
        std::set<const ICFGNode *> &identifyStart(std::set<const ICFGNode *> &container)
        {
            for (auto s = icfg->begin(); s != icfg->end(); s++)
            {
                const ICFGNode *iNode = s->second;
                if (iNode->getInEdges().empty())
                {
                    container.insert(iNode);
                }
            }
            return container;
        }

        ///identify the end assertion node on icfg
        std::set<const ICFGNode *> &identifyEnd(std::set<const ICFGNode *> &container)
        {
            for (const CallBlockNode *cs : pag->getCallSiteSet())
            {
                const SVFFunction *fun = SVFUtil::getCallee(cs->getCallSite());
                if (fun != NULL && fun->getName().str() == "assert")
                    container.insert(cs);
            }
            return container;
        }

        /// depth-first-search traversal on ICFG from src node to dst node
        void dfs(const ICFGNode *src, const ICFGNode *dst);

        void analyse();

        bool checkFeasibility(const ICFGNode *src, const ICFGNode *dst, std::vector<std::unordered_set<const ICFGNode *>> &cfgPaths);

    private:
        void addLoopToPath(std::vector<const ICFGNode *> &);

        void findValidCircle();

        PAG *pag;
        ICFG *icfg;
        std::set<std::vector<const ICFGNode *>> paths;
        ICFGECDetection *_icfgecDetection;
        int _k;
        std::unordered_set<int> _validCircle;
    };


}

#endif //SVF_EX_SSE_H