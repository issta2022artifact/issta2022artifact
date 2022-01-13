#ifndef EC_H_
#define EC_H_

#include "ECG.h"
#include "Util/SCC.h"
#include <stack>
#include <set>
#include <limits>

namespace SVF {

    typedef std::vector<NodeID> OneCycle;
    typedef SCCDetection<ECG *> SSEGSCC;//Tarjan
    template<class GraphType>
    class ECDetection {
    private:

        const GraphType &_graph;
        SSEGSCC *_scc;
        ECG *_ecg;

    public:

        ECDetection(const GraphType &G) : _graph(G) {
            // deep clone G
            _ecg = new ECG();
            for (auto it = _graph->begin(); it != _graph->end(); it++) {
                for (auto *edge : it->second->getOutEdges()) {
                    _ecg->addECGEdgesFromSrcDst(edge->getSrcID(), edge->getDstID());
                }
            }
            _scc = new SSEGSCC(_ecg);
        }

        ~ECDetection() {
            delete _ecg;
            delete _scc;
        }

        const inline GraphType &graph() {
            return _graph;
        }

        ///johnson's algorithm starts from here
        void simpleCycle() {
            NodeID startIndex = 1;
            NodeID maxNodeID = 0;
            for (auto it = _ecg->begin(); it != _ecg->end(); it++) {
                if (it->first > maxNodeID) {
                    maxNodeID = it->first;
                }
            }
            while (startIndex <= maxNodeID) {
                updateSubGraph(startIndex);
                _scc->find();
                NodeID leastIndex = leastIndexSCC();

                if (_ecg->hasECGNode(leastIndex) && _scc->isInCycle(leastIndex)) {
                    /// new _scc to be processed, clean all
                    _blockSet.clear();
                    _blockMap.clear();
                    findCyclesinSCG(leastIndex, leastIndex);
                    startIndex = leastIndex + 1;
                    while (startIndex < maxNodeID && !_ecg->hasECGNode(startIndex)) {
                        startIndex++;
                    }
                } else {
                    break;
                }
            }

            /// map <nodeId, _allCycles index>
            for (int i = 0; i < _allCycles.size(); ++i) {
                OneCycle &cycle = _allCycles[i];
                for (NodeID nodeId: cycle) {
                    _nodeIDtoCycleIndexBS[nodeId].insert(i);
                }
            }
            /// only for printing the results, can be commented 
//            for (OneCycle c : _allCycles) {
//                SVFUtil::outs() << "detected cycle:  " << " \n";
//                for (NodeID d : c) {
//                    SVFUtil::outs() << d;
//                }
//                SVFUtil::outs() << " \n";
//            }

        }

        inline bool isInCycle(NodeID nodeId) const {
            return _nodeIDtoCycleIndexBS.count(nodeId);
        }

        inline unordered_set<int> getNodeIDCycleIndexBs(NodeID nodeId) {
            assert(isInCycle(nodeId) && "nodeid should be in cycle!");
            return _nodeIDtoCycleIndexBS[nodeId];
        }


        inline const std::vector<OneCycle> &getAllCycles() const {
            return _allCycles;
        }

        /// get the current node's following rep node in _scc
        inline NodeBS getCurSCCsubNodes(NodeID n) {
            return _scc->subNodes(_scc->repNode(n));
        }


        /// check NodeID n whether in current NodeBS current node sub _scc set
        bool findElement(const NodeBS &bs, NodeID n) {
            for (NodeID s : bs) {
                if (n == s)
                    return true;
                else
                    continue;
            }
            return false;
        }

    private:

        SVF::NodeSet _blockSet;
        SVF::NodeVector _blockStack;
        Map<NodeID, SVF::NodeSet> _blockMap;
        std::vector<OneCycle> _allCycles;
        SVF::Map<NodeID, std::unordered_set<int>> _nodeIDtoCycleIndexBS;

        /// find least index in all sccs
        /// (_scc containing more than one node and self-cycle node)
        NodeID leastIndexSCC() {
            NodeID min = UINT_MAX;
            std::unordered_set<NodeID> visited;
            for (auto it = _ecg->begin(); it != _ecg->end(); it++) {
                NodeID rep = _scc->repNode(it->first);
                if (_scc->subNodes(rep).count() > 1) { // _scc containing more than one node
                    for (NodeID i: _scc->subNodes(rep)) {
                        if (!visited.count(i)) {
                            visited.insert(i);
                            min = (i < min) ? i : min;
                        }
                    }
                } else {  // self-cycle
                    if (!visited.count(it->first)) {
                        visited.insert(it->first);
                        for (ECGEdge *edge: it->second->getOutEdges()) {
                            if (edge->getDstID() == it->first) {
                                min = (it->first < min) ? it->first : min;
                            }
                        }
                    }
                }
            }
            return min;
        }

        /// create new graph only in _scc element.
        void updateSubGraph(NodeID n) {

            vector<NodeID> v;
            for (auto I = _ecg->begin(); I != _ecg->end(); I++) {
                // ECGNode* node = I->second;
                const NodeID id = I->first;
                if (id < n) {
                    v.push_back(id);
                }
            }

            for (NodeID id : v) {
                _ecg->removeECGNode(id);
            }
        }


        /// main handler: recursively detect elementary cycle
        bool findCyclesinSCG(NodeID start, NodeID cur) {
            bool foundCycle = false;
            _blockStack.push_back(cur);
            _blockSet.insert(cur);
            for (ECGEdge *e: _ecg->getECGNode(cur)->getOutEdges()) {
                ECGNode *n = e->getDstNode();
                NodeID nxt = n->getId();
                if (nxt == start) {
//                    _blockStack.push_back(nxt);
                    OneCycle cycle(_blockStack.begin(), _blockStack.end());
//                    _blockStack.pop_back();
                    _allCycles.push_back(cycle);
                    foundCycle = true;
                } else if (_blockSet.find(nxt) == _blockSet.end()) {
                    bool gotCycle = findCyclesinSCG(start, nxt);
                    foundCycle = foundCycle || gotCycle;
                } else
                    continue;
            }
            if (foundCycle)
                unblock(cur);
            else {
                for (ECGEdge *edge: _ecg->getECGNode(cur)->getOutEdges()) {
                    ECGNode *temp = edge->getDstNode();
                    NodeID next = temp->getId();
                    if (_blockMap.find(next) != _blockMap.end()) {
                        _blockMap[next].insert(cur);
                    }
                }

            }
            _blockStack.pop_back();
            return foundCycle;
        }

        /// unblock the _blockMap
        void unblock(NodeID n) {
            _blockSet.erase(n);
            if (_blockMap.find(n) != _blockMap.end()) {
                for (NodeID i: _blockMap[n]) {
                    if (_blockSet.find(i) != _blockSet.end())
                        unblock(i);
                    else
                        continue;
                }
                _blockMap.erase(n);
            }
        }

        void printSCC(NodeBS bs) {
            SVFUtil::outs() << "\n _scc components:   ";
            for (NodeID n:bs) {
                SVFUtil::outs() << n << "\t";
            }
            SVFUtil::outs() << "end \n   ";
        }
    };


} // namespace SVF

#endif  /* EC_H_ */