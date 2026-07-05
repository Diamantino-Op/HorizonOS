// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Evalyn Goemer & EvalynOS Contributors
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/branch/main/kernel/src/utils/dstruct/rbtree.c

#include "bstree.hpp"

void rbtree_insert_fixup(bstree_t* tree, bstree_node_t* node, bstree_node_t* parent, bstree_direction_t dir) {
    node->augment.rbColor = RB_RED;
    node->parent = parent;

    if (parent == nullptr) {
        tree->root = node;
        node->augment.rbColor = RB_BLACK;

        return;
    }

    parent->children[dir] = node;

    do {
        // case 1
        if (parent->augment.rbColor == RB_BLACK) {
            return;
        }

        bstree_node_t* grandparent = parent->parent;

        // case 4
        if (grandparent == nullptr) {
            parent->augment.rbColor = RB_BLACK;
            return;
        }

        dir = bstree_direction(parent);
        bstree_node_t* uncle = grandparent->children[1 - dir];
        if (uncle == nullptr or uncle->augment.rbColor == RB_BLACK) {
            // case 5
            if (node == parent->children[1 - dir]) {
                bstree_rotate_subtree(tree, parent, dir);
                node = parent;
                parent = grandparent->children[dir];
            }

            // case 6
            bstree_rotate_subtree(tree, grandparent, static_cast<bstree_direction_t>(1 - dir));
            parent->augment.rbColor = RB_BLACK;
            grandparent->augment.rbColor = RB_RED;

            return;
        }

        // case 2
        parent->augment.rbColor = RB_BLACK;
        uncle->augment.rbColor = RB_BLACK;
        grandparent->augment.rbColor = RB_RED;
        node = grandparent;

    } while ((parent = node->parent) != nullptr);

    // case 3
    tree->root->augment.rbColor = RB_BLACK;
}

static void __rbtree_remove_fixup(bstree_t* tree, bstree_node_t* node, bstree_direction_t dir) {
    bstree_node_t* parent = node->parent;

    if (parent == nullptr) {
        node->augment.rbColor = RB_BLACK;

        return;
    }

    bstree_node_t* sibling;
    bstree_node_t* close_nephew;
    bstree_node_t* distant_nephew;

    do {
        sibling = parent->children[1 - dir];
        distant_nephew = sibling->children[1 - dir];
        close_nephew = sibling->children[dir];

        if (sibling->augment.rbColor == RB_RED) {
            // case 3
            bstree_rotate_subtree(tree, parent, dir);
            parent->augment.rbColor = RB_RED;
            sibling->augment.rbColor = RB_BLACK;
            sibling = close_nephew;

            distant_nephew = sibling->children[1 - dir];

            if (distant_nephew != nullptr and distant_nephew->augment.rbColor == RB_RED) {
                goto case_6;
            }

            close_nephew = sibling->children[dir];

            if (close_nephew != nullptr and close_nephew->augment.rbColor == RB_RED) {
                goto case_5;
            }

            // case 4
            sibling->augment.rbColor = RB_RED;
            parent->augment.rbColor = RB_BLACK;

            return;
        }

        if (distant_nephew != nullptr and distant_nephew->augment.rbColor == RB_RED) {
            goto case_6;
		}

        if (close_nephew != nullptr and close_nephew->augment.rbColor == RB_RED) {
            goto case_5;
		}

        // case 4
        if (parent->augment.rbColor == RB_RED) {
            sibling->augment.rbColor = RB_RED;
            parent->augment.rbColor = RB_BLACK;

            return;
        }

        // case 2
        sibling->augment.rbColor = RB_RED;
        node = parent;

        if (node->parent == nullptr) {
        	break;
        }

        dir = bstree_direction(node);

    } while ((parent = node->parent) != nullptr);

    // case 1
    return;

case_5:

    bstree_rotate_subtree(tree, sibling, static_cast<bstree_direction_t>(1 - dir));
    sibling->augment.rbColor = RB_RED;
    close_nephew->augment.rbColor = RB_BLACK;
    distant_nephew = sibling;
    sibling = close_nephew;

case_6:

    bstree_rotate_subtree(tree, parent, dir);
    sibling->augment.rbColor = parent->augment.rbColor;
    parent->augment.rbColor = RB_BLACK;
    distant_nephew->augment.rbColor = RB_BLACK;
}

void rbtree_remove_fixup(bstree_t* tree, bstree_node_t* node, const bstree_node_t * parent, bstree_node_t* replacement, const bstree_direction_t dir) {
    if (node->augment.rbColor == RB_BLACK) {
        if (replacement != nullptr and replacement->augment.rbColor == RB_RED) {
            replacement->augment.rbColor = RB_BLACK;
        } else if (replacement != nullptr) {
            __rbtree_remove_fixup(tree, replacement, dir);
        } else if (parent != nullptr) {
            __rbtree_remove_fixup(tree, node, dir);
        }
    }
}
