
#ifndef SVF_SSE_TESTSSE_H
#define SVF_SSE_TESTSSE_H

// #include "Util/SCC.h"
// #include "ECG.h"
#include "EC.h"

using namespace SVF;
typedef SCCDetection<ECG *> ECGSCC;
typedef ECDetection<ECG*> ECD;

/// if the node is exit node, set cur node as exit node and set remain visit
/// return true for exit node
bool initExitNode(ECGNode *node, ECD *johnsonDetection, int k) {
    if (node->isExitNode())
        return true;
    if (!johnsonDetection->isInCycle(node->getId())) // not in circle->not exit node
        return false;

    for (auto it = node->getOutEdges().begin(); it != node->getOutEdges().end(); ++it) {
        ECGEdge *edge = *it;
        // find an out-loop-edge: do not have a loop in common
//        if (!johnsonDetection->isInCycle(edge->getDstID()) ||
//            !johnsonDetection->getNodeIDCycleIndexBs(edge->getSrcID()).intersects(
//                    johnsonDetection->getNodeIDCycleIndexBs(edge->getDstID()))
//                ) {
        if (!johnsonDetection->isInCycle(edge->getDstID()) ||
            johnsonDetection->getNodeIDCycleIndexBs(edge->getSrcID()) !=
            johnsonDetection->getNodeIDCycleIndexBs(edge->getDstID())
                ) {
            node->setExitNode();
            node->setRemainVisit(k + 1);
            return true;
        }
    }
    return false;
}

/// if the edge is in-loop edge, set cur edge as inloop edge and set remain visit
/// return true for in-loop edge
bool initInLoopEdge(ECGEdge *edge, ECD *johnsonDetection, int k) {
    if (edge->isInLoopEdge()) return true;
    // src or dst not in cycle
    if (!johnsonDetection->isInCycle(edge->getSrcID()) || !johnsonDetection->isInCycle(edge->getDstID()))
        return false;

    // have loop in common
//    if (johnsonDetection->getNodeIDCycleIndexBs(edge->getSrcID()).intersects(
//            johnsonDetection->getNodeIDCycleIndexBs(edge->getDstID()))) {
    if (johnsonDetection->getNodeIDCycleIndexBs(edge->getSrcID()) ==
        johnsonDetection->getNodeIDCycleIndexBs(edge->getDstID())) {
        edge->setInLoopEdge();
        edge->setRemainVisit(k);
        return true;
    }
    return false;
}

/// initialize the times (k+1) needed to visit exit node
/// label exit node and in-loop edges from exit node
void initGraph(ECG *ecg, ECD *johnsonDetection, int k) {
    // init via ecg
    for (auto it = ecg->begin(); it != ecg->end(); it++) {
        if (initExitNode(it->second, johnsonDetection, k)) {
            for (ECGEdge *edge: it->second->getOutEdges()) {
                initInLoopEdge(edge, johnsonDetection, k);
            }
        }
    }
    return;
}

/// print path on ECG
void printPath(std::vector<const ECGNode *> path) {
    SVFUtil::outs() << "<START> ";
    for (const ECGNode *node:path) {
        SVFUtil::outs() << node->getId() << " ";
    }
    SVFUtil::outs() << "<END>\n";
}

void
dfs(ECGNode *cur, ECGNode *dst, std::vector<const ECGNode *> &path, ECD *johnsonDetection, int &k) {
    if (cur->isExitNode())
        cur->setRemainVisit(cur->getRemainVisit() - 1); // decrease remaining visit for br node by 1
    path.push_back(cur); // update cur path
    if (cur == dst)
        printPath(path); // reach target sink node
    else {
        if (cur->isExitNode()) {
            if (cur->getRemainVisit() == 0) { // loop has been visited for k times
                cur->setRemainVisit(k + 1); // reset remain visited time
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (!edge->isInLoopEdge()) { // break loop
                        dfs(edge->getDstNode(), dst, path, johnsonDetection, k);
                    }
                }
                cur->setRemainVisit(0);
            } else { // loop is still being visited
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (edge->isInLoopEdge()) { // force visit in loop
                        dfs(edge->getDstNode(), dst, path, johnsonDetection, k);
                    }
                }
            }
        } else {
            for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                ECGEdge *edge = *it;
                dfs(edge->getDstNode(), dst, path, johnsonDetection, k);
            }
        }
    }
    // backtrace
    if (cur->isExitNode())
        cur->setRemainVisit(cur->getRemainVisit() + 1);
    path.pop_back();

    return;
}

void
dfs_edge(ECGNode *cur, ECGNode *dst, std::vector<const ECGNode *> &path,
         ECD *johnsonDetection, int &k) {
    path.push_back(cur); // update cur path
    if (cur == dst)
        printPath(path); // reach target sink node
    else {
        if (cur->isExitNode()) {
            bool forceInLoop = false;
            for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                ECGEdge *edge = *it;
                if (edge->isInLoopEdge()) {
                    if (edge->getRemainVisit() != 0) { // still have in-loop edges to visit
                        forceInLoop = true;
                    }
                }
            }

            if (!forceInLoop) { // go out-loop edges
                // reset remain visit times for in-loop edges
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (edge->isInLoopEdge()) {
                        edge->setRemainVisit(k + 1);
                    }
                }
                // out-loop visit
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (!edge->isInLoopEdge()) {
                        dfs_edge(edge->getDstNode(), dst, path, johnsonDetection, k);
                    }
                }
                // backtrace
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (edge->isInLoopEdge()) { // in-loop edges
                        edge->setRemainVisit(0);
                    }
                }

            } else { // in-loop edges should still be visited
                for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                    ECGEdge *edge = *it;
                    if (edge->isInLoopEdge()) {
                        if (edge->getRemainVisit() != 0) {
                            edge->setRemainVisit(edge->getRemainVisit() - 1);
                            dfs_edge(edge->getDstNode(), dst, path, johnsonDetection, k);
                            edge->setRemainVisit(edge->getRemainVisit() + 1);
                        }
                    }
                }
            }
        } else { // cur node is not exit node
            for (ECGNode::const_iterator it = cur->OutEdgeBegin(); it != cur->OutEdgeEnd(); ++it) {
                ECGEdge *edge = *it;
                dfs_edge(edge->getDstNode(), dst, path, johnsonDetection, k);
            }
        }
    }
    // backtrace
    path.pop_back();
}

std::set<std::vector<ECGNode *>> dfs(ECGNode *src, ECGNode *dst) {
    typedef std::pair<ECGNode *, int> Order;
    std::stack<Order> worklist;
    std::set<std::vector<ECGNode *>> paths;
    std::vector<ECGNode *> path;
    std::set<ECGNode *> visited;
    worklist.push(std::make_pair(src, 0));
    while (!worklist.empty()) {
        Order cur = worklist.top();
        worklist.pop();
        while (!path.empty() and cur.second < path.size()) {
            ECGNode *last = path.back();
            path.pop_back();
            visited.erase(last);
        }
        int index = cur.second + 1;
        if (visited.find(cur.first) == visited.end()) {
            path.push_back(cur.first);
            visited.insert(cur.first);
            if (cur.first == dst) {
                paths.insert(path);
            }
            for (ECGEdge *edge : cur.first->getOutEdges()) {
                worklist.push(std::make_pair(edge->getDstNode(), index));
            }
        }
    }
    // for(vector<ECGNode *> sp: paths)
    // {
    //     cout << "path: " ;
    //     for(ECGNode* node: sp)
    //     {
    //         cout << node->toString();
    //     }
    //     cout << "\n " ;
    // }
    return paths;
}

void test01() {
    SVFUtil::outs() << "test1:\n";
    /*
        1->2->3->4->5
           \   /
            6
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {4, 6},
                       {6, 2}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test01");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(5), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
    // assert(dfs(ecg->getECGNode(1), ecg->getECGNode(6)) == )
}

void test02() {
    SVFUtil::outs() << "\ntest2:\n";
    /*
                7
                 \
        1->2->3->4->5
           \   /
            6
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {4, 6},
                       {6, 2},
                       {7, 4}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test02");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(5), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void test03() {
    SVFUtil::outs() << "\ntest3:\n";
    /*
        1->2->3->4->5->6
           \     /
              7
             /
            8
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {7, 8}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test03");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;
    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;

}

void test04() {
    SVFUtil::outs() << "\ntest4:\n";
    /*
        1->2->3->4->5->6
           \     /
              7
            /^
          8
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {8, 7}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test04");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;

}

void test05() {
    SVFUtil::outs() << "\ntest5:\n";
    /*      8
          /  \
        1->2->3->4->5->6
           \     /
              7
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {1, 8},
                       {8, 3}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test05");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void test06() {
    SVFUtil::outs() << "\ntest6:\n";
    /*
                 8
             /     \
        1->2->3->4->5->6
           \     /
              7
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {5, 8},
                       {8, 2}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test06");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void test07() {
    SVFUtil::outs() << "\ntest7:\n";
    /*
                 8
             /     \
        1->2->3->4->5->6
           \     /
           |  7
           | /
           9

              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 1},
                       {1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {5, 8},
                       {8, 2},
                       {2, 9},
                       {9, 7},
                       {4, 8}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test07");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

//    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void test08() {
    SVFUtil::outs() << "\ntest8:\n";
    /*
                 8
             /     \
        1->2->3->4->5->6
           \     /
              7
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1, 2},
                       {2, 3},
                       {3, 4},
                       {4, 5},
                       {5, 6},
                       {5, 7},
                       {7, 2},
                       {5, 8},
                       {8, 2},
                       {3, 9},
                       {9, 2}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test08");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void test09() {
    SVFUtil::outs() << "\ntest9:\n";
    /*
              8     10
            / 9  \/   \
        1->2->3->4->5->6
           \     /
              7
              */
    ECG *ecg = new ECG();
    NodePairVector nPV{{1,  2},
                       {2,  3},
                       {3,  4},
                       {4,  5},
                       {5,  6},
                       {5,  7},
                       {7,  2},
                       {5,  8},
                       {8,  2},
                       {3,  9},
                       {9,  2},
                       {6,  10},
                       {10, 4}};
    ecg->addECGEdgesFromVector(nPV);
    ecg->dump("../../test09");
    ECD *johnsonDetection = new ECD(ecg);
    johnsonDetection->simpleCycle();

    int k = 2;
    initGraph(ecg, johnsonDetection, k);
    std::vector<const ECGNode *> path;

    dfs_edge(ecg->getECGNode(1), ecg->getECGNode(6), path, johnsonDetection, k);
    delete johnsonDetection;
    delete ecg;
}

void TestECG() {
//    test01();
//    test02();
//    test03();
//    test04();
//    test05();
//    test06();
    test07();
//    test08();
//    test09();

}


#endif