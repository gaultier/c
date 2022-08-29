#include <_types/_uint64_t.h>
#include <assert.h>
#include <inttypes.h>
#include <malloc/_malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node_t {
    struct node_t *left, *right;
    int val;
};
typedef struct node_t node_t;

struct link_t {
    node_t* node;
    struct link_t* next;
};
typedef struct link_t link_t;

link_t* new_link(node_t* node) {
    link_t* link = malloc(sizeof(link_t));
    link->node = node;
    link->next = NULL;
    return link;
}

int main() {
    node_t nodes[6] = {0};
    nodes[0] = (node_t){.val = 100, .left = &nodes[1], .right = &nodes[2]};
    nodes[1] = (node_t){.val = 50, .left = &nodes[3], .right = &nodes[4]};
    nodes[2] = (node_t){.val = 200, .right = &nodes[5]};
    nodes[3] = (node_t){.val = 25};
    nodes[4] = (node_t){.val = 75};
    nodes[5] = (node_t){.val = 350};

    link_t *current = new_link(nodes), *next = NULL;

    while (1) {
        while (current != NULL) {
            const node_t* const node = current->node;
            assert(node != NULL);
            printf("%d ", node->val);

            if (node->right != NULL) {
                link_t* next_right = new_link(node->right);
                next_right->next = next;
                next = next_right;
            }
            if (node->left != NULL) {
                link_t* next_left = new_link(node->left);
                next_left->next = next;
                next = next_left;
            }
            current = current->next;
        }
        puts("");
        if (next == NULL) break;

        link_t* tmp = current;
        current = next;
        next = tmp;
    }
}
