#ifndef VCL_H_
#define VCL_H_

#include "SSE.h"

namespace SVF
{
    struct VFlow
    {
        std::string id;
        std::vector<std::pair<u32_t, u32_t>> statements;
        std::vector<s32_t> vfgNodeKinds;
        std::vector<u32_t> nodeIDs;
        std::vector<std::string> apis;
        std::vector<s32_t> types;
    };
    class VFPExtractor
    {
    public:
        VFPExtractor() {}
        ~VFPExtractor()
        {
            for (auto I = _vfps.begin(), E = _vfps.end(); I != E; ++I)
                delete (*I);
        }

        /// extract method-level value flow paths
        void extractMethodVFPs(SVFG *svfg, std::string methodName, std::string apiPath, const std::string fileName, const std::string OUTPUT);
        /// extract slice-level value flow paths
        void extraceSliceVFPs(SVFG *svfg, const std::string apiPath, const std::string OUTPUT);
        /// filter vfps using sse checker
        void filterVFPs(SVFG *svfg, const std::string &output);

        friend llvm::raw_ostream &operator<<(llvm::raw_ostream &o, VFPExtractor &vcl)
        {
            for (const VFlow *vfp : vcl._vfps)
            {
                if (vfp->statements.size() > 1)
                    vcl.outputVFP(o, vfp);
            }
            return o;
        }
        /// dump value flow path to a tmp file
        void dump(const std::string &pname);

    private:
        void initVulAPI(const std::string apiPath);
        /// whether the svfgNode is a function exit node
        inline bool isMethodExit(const SVFGNode *svfgNode, std::string methodName, std::string fileName);
        /// Get the meta data (line number and file name) info of a LLVM value
        u32_t getLineNumber(const Value *val);
        /// Get the line number of a SVFG node
        inline u32_t getSVFGLineNumber(const SVFGNode *svfgNode);
        /// Get the value of a SVFG node
        const Value *getSVFGValue(const SVFGNode *svfgNode);
        /// Get the API call name of a call SVFG node
        inline std::vector<std::string> getAPI(const SVFGNode *svfgNode, SVFG *svfg);
        /// intraprocedurally traverse SVFG to obtain value flow paths
        void intraTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::string methodName, std::string fileName, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res);
        /// backward traverse SVFG
        void backTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res, std::vector<std::pair<char, CallSiteID>> &callsites, std::vector<std::vector<std::pair<char, CallSiteID>>> &allCallsites);
        /// forward traverse SVFG
        void forTraverseSVFG(const SVFGNode *cur, SVFG *svfg, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path, std::vector<const VFlow *> &res, std::vector<std::pair<char, CallSiteID>> &callsites, std::vector<std::vector<std::pair<char, CallSiteID>>> &allCallsites);
        /// merge forward and backward vfps using cfl
        void mergeVFPs(std::vector<const VFlow *> &bk_res, std::vector<std::vector<std::pair<char, CallSiteID>>> &bk_allCallsites, std::vector<const VFlow *> &f_res, std::vector<std::vector<std::pair<char, CallSiteID>>> &f_allCallsites, const SVFGNode *src, SVFG *svfg);
        /// whether the target callsites satisfy cfl
        bool satisfyCFL(std::vector<std::pair<char, CallSiteID>> &callsites);
        /// output one value flow path
        void outputVFP(llvm::raw_ostream &os, const VFlow *vfp);
        /// output source code files
        void outputFiles(const string fileName);
        /// sse checker
        bool satisfySSE(const std::string &s_nodeIDs, SVFG *svfg, Traversal *traversal);
        /// whether the line number is false
        bool isWrongLineNum(const SVFGNode *svfgNode);
        /// whether there exists redefine between svfgNode1 and svfgNode2
        bool hasRedefine(SVFG *svfg, const SVFGNode *svfgNode1, const SVFGNode *svfgNode2, std::unordered_set<const SVF::ICFGNode *> &cfgPath);
        /// get cfg node between svfgNode1 and svfgNode2 on SVFG
        void getInBetweenSVFGNodes(const SVFGNode *svfgNode1, const SVFGNode *svfgNode2, SVFG *svfg, std::unordered_set<const SVF::ICFGNode *> &svfgNodes);
        /// traverse SVFG to get cfg node between svfgNode1 and svfgNode2
        void traverseSVFGNodePair(const SVFGNode *cur, const SVFGNode *svfgNode2, SVFG *svfg, std::unordered_set<const SVF::ICFGNode *> &svfgNodes, std::unordered_set<const SVFGNode *> &visited, std::vector<const SVFGNode *> &path);
        /// record type info
        void recordType(const SVFGNode *svfgNode, std::vector<s32_t> &types);
        /// get file name
        const std::string getFileName(const Value *val);
        /// get svfg node file name
        inline const std::string getSVFGFileName(const SVFGNode *svfgNode);

    private:
        std::vector<const VFlow *> _vfps;
        std::unordered_set<std::string> _vulAPIS;
        std::unordered_map<std::string, u32_t> _file_to_idx;
        std::vector<std::string> _files;
        u32_t _mode;
    };

} // namespace SVF

#endif /* VCL_H_ */