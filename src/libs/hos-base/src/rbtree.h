// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Evalyn Goemer & EvalynOS Contributors
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/branch/main/kernel/src/utils/dstruct/rbtree.h

#ifndef LIB_HOS_BASE_RBTREE_HPP
#define LIB_HOS_BASE_RBTREE_HPP

#include "bstree.hpp"

void rbtree_insert_fixup(bstree_t* tree, bstree_node_t* node, bstree_node_t* parent, bstree_direction_t dir);
void rbtree_remove_fixup(bstree_t* tree, bstree_node_t* node, const bstree_node_t * parent, bstree_node_t* replacement, bstree_direction_t dir);

#endif