#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node_t {
    struct node_t *next, *arbitrary;
    int val;
};
typedef struct node_t node_t;

node_t* clone_linked_list(node_t* nodes, uint64_t nodes_len) {
    node_t* clones = malloc(sizeof(node_t) * nodes_len);
    memcpy(clones, nodes, nodes_len * sizeof(node_t));

    for (uint64_t i = 0; i < nodes_len; i++) {
        node_t* node = &nodes[i];
        node_t* clone = &clones[i];

        const uint64_t next_offset = (node->next - nodes);
        assert(next_offset <= nodes_len);
        const uint64_t arbitrary_offset = (node->arbitrary - nodes);
        assert(arbitrary_offset <= nodes_len);

        clone->next = &clones[next_offset];
        clone->arbitrary = &clones[arbitrary_offset];
    }
    return clones;
}

int main() {
    node_t nodes[3] = {0};
    nodes[0] = (node_t){.val = 1, .next = &nodes[2], .arbitrary = &nodes[1]};
    nodes[1] = (node_t){.val = 2, .next = &nodes[0], .arbitrary = &nodes[0]};
    nodes[2] = (node_t){.val = 3, .next = &nodes[2], .arbitrary = &nodes[1]};
    const uint64_t nodes_len = sizeof(nodes) / sizeof(nodes[0]);

    for (uint64_t i = 0; i < nodes_len; i++) {
        node_t* node = &nodes[i];
        printf("%p %p %p %d\n", node, node->next, node->arbitrary, node->val);
    }

    node_t* clones = clone_linked_list(nodes, nodes_len);
    for (uint64_t i = 0; i < nodes_len; i++) {
        node_t* clone = &clones[i];
        printf("%p %p %p %d\n", clone, clone->next, clone->arbitrary,
               clone->val);
    }
}
