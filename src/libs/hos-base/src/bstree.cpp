// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Evalyn Goemer & EvalynOS Contributors

#include "bstree.hpp"

#include "cstdint"
#include "rbtree.h"
#include "assert.h"

auto bstree_minimum(bstree_node_t *node) -> bstree_node_t* {
    while (node->left != nullptr) {
    	node = node->left;
    }

    return node;
}

auto bstree_maximum(bstree_node_t *node) -> bstree_node_t* {
    while (node->right != nullptr) {
    	node = node->right;
    }

    return node;
}

auto bstree_successor(const bstree_node_t *node) -> bstree_node_t* {
    if (node->right != nullptr) {
    	return bstree_minimum(node->right);
    }

    bstree_node_t* parent = node->parent;

    while (parent != nullptr and node == parent->right) {
        node = parent;
        parent = parent->parent;
    }

    return parent;
}

auto bstree_predecessor(const bstree_node_t *node) -> bstree_node_t* {
    if (node->left != nullptr) {
    	return bstree_maximum(node->left);
    }

    bstree_node_t* parent = node->parent;

    while (parent != nullptr and node == parent->left) {
        node = parent;
        parent = parent->parent;
    }

    return parent;
}

static auto bstree_search_exact(const bstree_t *tree, const u64 query) -> bstree_node_t* {
    bstree_node_t* node = tree->root;

    while (node != nullptr) {
        const u64 val = tree->value_of_node(node);

        if (query < val) {
        	node = node->left;
        } else if (query > val) {
        	node = node->right;
        } else {
        	return node;
        }
    }

    return nullptr;
}

static auto bstree_search_nearest(const bstree_t *tree, const u64 query) -> bstree_node_t* {
    bstree_node_t* node = tree->root;
    bstree_node_t* nearest = nullptr;
    u64 best = UINT64_MAX;

    while (node != nullptr) {
        const u64 val = tree->value_of_node(node);
        const u64 dist = val > query ? val - query : query - val;

        if (dist < best) {
            best = dist;
            nearest = node;
        }

        if (query < val) {
            node = node->left;
        } else if (query > val) {
            node = node->right;
        } else {
            return node;
		}
    }

    return nearest;
}

auto bstree_rotate_subtree(bstree_t *tree, bstree_node_t *sub, const bstree_direction_t dir) -> bstree_node_t* {
    bstree_node_t* sub_parent = sub->parent;
    bstree_node_t* new_root = sub->children[1 - dir];
    bstree_node_t* new_child = new_root->children[dir];

    sub->children[1 - dir] = new_child;

    if (new_child != nullptr) {
        new_child->parent = sub;
	}

    new_root->children[dir] = sub;
    new_root->parent = sub_parent;
    sub->parent = new_root;

    if (sub_parent != nullptr) {
        sub_parent->children[sub == sub_parent->right] = new_root;
    } else {
        tree->root = new_root;
	}

    return new_root;
}

static auto bstree_search_lesser(const bstree_t *tree, const u64 query, const bool find_equal) -> bstree_node_t* {
    bstree_node_t* node = tree->root;
    bstree_node_t* candidate = nullptr;

    while (node != nullptr) {
		const u64 val = tree->value_of_node(node);

        if (val < query) {
            candidate = node;
            node = node->right;
        } else if (val > query) {
            node = node->left;
        } else {
            if (find_equal) {
            	return node;
			}

            candidate = node->left != nullptr ? bstree_maximum(node->left) : candidate;

            return candidate;
        }
    }

    return candidate;
}

static auto bstree_search_greater(const bstree_t *tree, const u64 query, const bool find_equal) -> bstree_node_t* {
    bstree_node_t* node = tree->root;
    bstree_node_t* candidate = nullptr;

    while (node != nullptr) {
        const u64 val = tree->value_of_node(node);

        if (val > query) {
            candidate = node;
            node = node->left;
        } else if (val < query) {
            node = node->right;
        } else {
            if (find_equal) {
            	return node;
            }

            candidate = node->right != nullptr ? bstree_minimum(node->right) : candidate;

            return candidate;
        }
    }

    return candidate;
}

auto bstree_search(const bstree_t *tree, const u64 query, const bstree_search_type_t type) -> bstree_node_t* {
    switch (type) {
        case BST_SEARCH_TYPE_EXACT:
    		return bstree_search_exact(tree, query);

        case BST_SEARCH_TYPE_NEAREST:
    		return bstree_search_nearest(tree, query);

        case BST_SEARCH_TYPE_NEAREST_LT:
    		return bstree_search_lesser(tree, query, false);

        case BST_SEARCH_TYPE_NEAREST_LTE:
    		return bstree_search_lesser(tree, query, true);

        case BST_SEARCH_TYPE_NEAREST_GT:
    		return bstree_search_greater(tree, query, false);

        case BST_SEARCH_TYPE_NEAREST_GTE:
    		return bstree_search_greater(tree, query, true);

        default:
    		__builtin_unreachable();
    }
}

auto bstree_insert(bstree_t* tree, bstree_node_t* node) -> bstree_node_t* {
    node->left = nullptr;
    node->right = nullptr;
    node->parent = nullptr;

    bstree_node_t* parent = nullptr;
    bstree_node_t* cur = tree->root;
    bstree_direction_t dir = BST_LEFT;

	const u64 key = tree->value_of_node(node);

    while (cur != nullptr) {
        parent = cur;

		const u64 cur_val = tree->value_of_node(cur);

        if (key < cur_val) {
            dir = BST_LEFT;
            cur = cur->left;
        } else {
            dir = BST_RIGHT;
            cur = cur->right;
        }
    }

    if (parent == nullptr) {
        tree->root = node;
        node->parent = nullptr;
    } else {
        node->parent = parent;
        parent->children[dir] = node;
    }

    switch (tree->type) {
        case BST_TYPE_NORM:
    		return node;

        case BST_TYPE_RB:
    		rbtree_insert_fixup(tree, node, parent, dir);
    		return node;

        case BST_TYPE_AVL:
    		assert("unimplemented" && false);

		default:
    		__builtin_unreachable();
    }
}

auto bstree_remove(bstree_t* tree, bstree_node_t* node) -> bstree_node_t* {
    bstree_node_t* parent = node->parent;
    bstree_node_t* replacement = nullptr;
    bstree_direction_t dir = BST_LEFT;

    if (node->left != nullptr and node->right != nullptr) {
        bstree_node_t* succ = bstree_minimum(node->right);
        bstree_node_t* succ_parent = succ->parent;
        bstree_node_t* succ_right  = succ->right;

        succ->left = node->left;
        succ->left->parent = succ;

        succ->parent = node->parent;
        if (node->parent == nullptr) {
            tree->root = succ;
        } else {
            node->parent->children[node == node->parent->right] = succ;
		}

        if (succ_parent == node) {
            succ->right = node;
            node->parent = succ;
        } else {
            succ->right = node->right;
            succ->right->parent = succ;
            succ_parent->left = node;
            node->parent = succ_parent;
        }

        node->left  = nullptr;
        node->right = succ_right;

        if (node->right != nullptr) {
        	node->right->parent = node;
		}

		const auto tmp = succ->augment;
        succ->augment = node->augment;
        node->augment = tmp;

        parent = node->parent;
    }

    replacement = node->left != nullptr ? node->left : node->right;

    if (replacement != nullptr) {
        replacement->parent = parent;
	}

    if (parent == nullptr) {
        tree->root = replacement;
    } else {
        dir = bstree_direction(node);
        parent->children[dir] = replacement;
    }

    switch (tree->type) {
        case BST_TYPE_NORM:
    		break;

        case BST_TYPE_RB:
    		rbtree_remove_fixup(tree, node, parent, replacement, dir);
    		break;

        case BST_TYPE_AVL:
    		assert("unimplemented" && false);

		default:
    		__builtin_unreachable();
    }

    return replacement;
}
