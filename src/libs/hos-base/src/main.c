#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "bstree.h"

typedef struct {
    bstree_node_t node;
    uint64_t value;
} mynode_t;

static uint64_t get_value(bstree_node_t* node) {
    mynode_t* n = CONTAINER_OF(node, mynode_t, node);
    return n->value;
}

int main() {
    bstree_t tree = {0};
    tree.value_of_node = get_value;
    tree.type = BST_TYPE_RB;

    mynode_t* a = malloc(sizeof(*a));
    mynode_t* b = malloc(sizeof(*b));
    mynode_t* c = malloc(sizeof(*c));

    a->value = 10;
    b->value = 20;
    c->value = 30;

    printf("insert: %lu\n", a->value);
    bstree_insert(&tree, &a->node);

    printf("insert: %lu\n", b->value);
    bstree_insert(&tree, &b->node);

    printf("insert: %lu\n", c->value);
    bstree_insert(&tree, &c->node);

    uint64_t keys[] = {10, 20, 30, 40};

    for (size_t i = 0; i < 4; i++) {
        uint64_t k = keys[i];
        printf("search: %lu\n", k);

        bstree_node_t* found = bstree_search(&tree, k, 0);

        if (found) {
            mynode_t* mf = CONTAINER_OF(found, mynode_t, node);
            printf("found: %lu\n", mf->value);
        } else {
            printf("not found: %lu\n", k);
        }
    }

    printf("remove: %lu\n", b->value);
    bstree_remove(&tree, &b->node);
    free(b);

    printf("remove: %lu\n", a->value);
    bstree_remove(&tree, &a->node);
    free(a);

    printf("remove: %lu\n", c->value);
    bstree_remove(&tree, &c->node);
    free(c);

    return 0;
}
