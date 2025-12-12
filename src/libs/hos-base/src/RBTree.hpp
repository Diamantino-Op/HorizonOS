#ifndef LIB_HOS_BASE_RBTREE_HPP
#define LIB_HOS_BASE_RBTREE_HPP

#include "Types.hpp"

enum class RBTreeColor {
    RED,
    BLACK
};

struct RBTreeNode {
    u64 data = {};
    u64 extraData = {};
    RBTreeColor color = RBTreeColor::RED;
    RBTreeNode *left = {};
    RBTreeNode *right = {};
    RBTreeNode *parent = {};
};

using CreateNodeFun = RBTreeNode*(*)(u64 data, u64 extraData, u64 *extraArgs);
using DeleteNodeFun = void(*)(RBTreeNode *node, u64 *extraArgs);

// TODO: Change new with Allocator
class RBTree {
public:
    void insert(u64 data, u64 extraData, u64 *extraArgs, CreateNodeFun createNode = defaultCreateNode);

    void remove(u64 data, u64 *extraArgs, DeleteNodeFun deleteNode = defaultDeleteNode);

    RBTreeNode *find(u64 data);

    RBTreeNode *min();

private:
    static RBTreeNode *defaultCreateNode(u64 data, u64 extraData = 0, u64 *extraArgs = nullptr);
    static void defaultDeleteNode(RBTreeNode *node, u64 *extraArgs = nullptr);

    void rotateLeft(RBTreeNode *node);

    void rotateRight(RBTreeNode *node);

    void fixInsert(RBTreeNode *node);
    void fixDelete(RBTreeNode *node);

    RBTreeNode *findNode(RBTreeNode *node, u64 data);

    RBTreeNode *minNode(RBTreeNode *node);

    void transplant(const RBTreeNode *node1, RBTreeNode *node2);

private:

    RBTreeNode *root = nullptr;
};

#endif
