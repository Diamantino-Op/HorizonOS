// Code edited from EvalynGoemer's bstree.h

#ifndef LIB_HOS_BASE_BSTREE_HPP
#define LIB_HOS_BASE_BSTREE_HPP

#include "Types.hpp"

template <typename T, typename M>
constexpr auto container_of(M* ptr, M T::* member) -> T* {
	const uPtr member_offset =reinterpret_cast<uPtr>(&(reinterpret_cast<T const volatile*>(0)->*member));

	return reinterpret_cast<T*>(reinterpret_cast<uPtr>(ptr) - member_offset);
}

enum bstree_direction_t : uFast8 {
    BST_LEFT,
    BST_RIGHT,
};

enum bstree_rbcolor_t : u32 {
    RB_BLACK,
    RB_RED,
};

enum bstree_search_type_t : u16 {
    BST_SEARCH_TYPE_EXACT,
    BST_SEARCH_TYPE_NEAREST,
    BST_SEARCH_TYPE_NEAREST_LT,
    BST_SEARCH_TYPE_NEAREST_LTE,
    BST_SEARCH_TYPE_NEAREST_GT,
    BST_SEARCH_TYPE_NEAREST_GTE,
};

enum bstree_tree_type_t : u16 {
    BST_TYPE_NORM,
    BST_TYPE_RB,
    BST_TYPE_AVL,
};

struct bstree_node_t;

struct bstree_t {
    u64 (*value_of_node)(bstree_node_t* node);
    bstree_node_t* root;
    bstree_tree_type_t type;
};

struct bstree_node_t {
    bstree_node_t* parent;

    union {
        struct {
            bstree_node_t* left;
            bstree_node_t* right;
        };

        bstree_node_t* children[2];
    };

    union {
        bstree_rbcolor_t rbColor;
        i32 avlHeight;
    } augment;
};

inline auto bstree_direction(const bstree_node_t* node) -> bstree_direction_t {
    return node == node->parent->right ? bstree_direction_t::BST_RIGHT : bstree_direction_t::BST_LEFT;
}

auto bstree_search(const bstree_t* tree, u64 query, bstree_search_type_t type) -> bstree_node_t*;
auto bstree_insert(bstree_t* tree, bstree_node_t* node) -> bstree_node_t*;
auto bstree_remove(bstree_t* tree, bstree_node_t* node) -> bstree_node_t*;

auto bstree_rotate_subtree(bstree_t* tree, bstree_node_t* sub, bstree_direction_t dir) -> bstree_node_t*;
auto bstree_minimum(bstree_node_t* node) -> bstree_node_t*;
auto bstree_maximum(bstree_node_t* node) -> bstree_node_t*;
auto bstree_successor(const bstree_node_t* node) -> bstree_node_t*;
auto bstree_predecessor(const bstree_node_t* node) -> bstree_node_t*;

#endif