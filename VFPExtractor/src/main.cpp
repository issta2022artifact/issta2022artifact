#include "VFPExtractor.h"

using namespace llvm;
using namespace SVF;

static llvm::cl::opt<std::string> InputFilename(cl::Positional,
                                                llvm::cl::desc("<input bitcode>"), llvm::cl::init("-"));

static llvm::cl::opt<int> MODE(
    "mode",
    llvm::cl::init(1),
    llvm::cl::desc("1 for method-level vfps extraction, 2 for slice-level, 3 for sse filtering"));

static llvm::cl::opt<std::string> METHODNAME("method-name", llvm::cl::init("main"),
                                             llvm::cl::desc("name of the method"));

static llvm::cl::opt<std::string> FILENAME("file-name", llvm::cl::init("file.c"),
                                           llvm::cl::desc("name of the file"));

static llvm::cl::opt<std::string> APIPATH("api-path", llvm::cl::init("data/API.txt"),
                                          llvm::cl::desc("path to api list"));

static llvm::cl::opt<std::string> OUTPUT("output", llvm::cl::init("data/vfps_tmp/"),
                                         llvm::cl::desc("output file of value flows"));

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

    /// Create Andersen's pointer analysis
    Andersen *ander = AndersenWaveDiff::createAndersenWaveDiff(pag);

    /// Sparse value-flow graph (SVFG)
    SVFGBuilder svfBuilder;
    SVFG *svfg = svfBuilder.buildFullSVFGWithoutOPT(ander);
    VFPExtractor *vcl = new VFPExtractor();

    switch (MODE)
    {
    case 1:
        /* method-level vfps extraction */
        vcl->extractMethodVFPs(svfg, METHODNAME, APIPATH, FILENAME, OUTPUT);
        break;
    case 2:
        /* slice-level vfps extraction */
        vcl->extraceSliceVFPs(svfg, APIPATH, OUTPUT);
        break;
    case 3:
        /* sse filtering */
        vcl->filterVFPs(svfg, OUTPUT);
        break;
    default:
        break;
    }

    SVF::LLVMModuleSet::releaseLLVMModuleSet();
    SVF::PAG::releasePAG();
    // delete traversal;

    return 0;
}