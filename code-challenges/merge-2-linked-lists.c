#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct item_t {
    int val;
    int next_i;
};
typedef struct item_t item_t;

static void merge_sorted_linked_lists_do(item_t* nodes, int lesser_i,
                                         int greater_i) {
    if (lesser_i == -1 || greater_i == -1) return;

    item_t* lesser = &nodes[lesser_i];
    item_t* greater = &nodes[greater_i];

    item_t* lesser_next = lesser->next_i == -1 ? NULL : &nodes[lesser->next_i];
    // Merge greater into lesser if it is suitable i.e. lesser->val <
    // greater->val < lesser_next->val, with NULL checks
    if (lesser_next == NULL ||
        (lesser_next != NULL && lesser_next->val > greater->val)) {
        int greater_next_i = greater->next_i;
        greater->next_i = lesser->next_i;
        lesser->next_i = greater_i;

        // `greater` now points to the next item in the `greater` list,
        // since we removed its head, or NULL if it is now empty
        greater_i = greater_next_i;
    } else if (lesser_next != NULL && lesser_next->val <= greater->val) {
        lesser_i = lesser->next_i;
    }
    merge_sorted_linked_lists_do(nodes, lesser_i, greater_i);
}

static void merge_sorted_linked_lists(item_t* nodes, int lesser_i,
                                      int greater_i, item_t** head) {
    assert(head != NULL);
    if (lesser_i == -1 || greater_i == -1) return;

    item_t* lesser = &nodes[lesser_i];
    item_t* greater = &nodes[greater_i];
    // Swap if passed the wrong way around
    if (lesser->val > greater->val) {
        item_t* tmp = lesser;
        lesser = greater;
        greater = tmp;
    }
    *head = lesser;

    merge_sorted_linked_lists_do(nodes, lesser_i, greater_i);
}

int main() {
    item_t nodes[] = {
        [0] = {.val = 19, .next_i = -1}, [1] = {.val = 15, .next_i = 0},
        [2] = {.val = 8, .next_i = 1},   [3] = {.val = 4, .next_i = 2},
        [4] = {.val = 16, .next_i = -1}, [5] = {.val = 10, .next_i = 4},
        [6] = {.val = 9, .next_i = 5},   [7] = {.val = 7, .next_i = 6},
    };

    item_t* head = NULL;
    merge_sorted_linked_lists(nodes, 3, 7, &head);

    item_t* node = head;
    while (node != NULL) {
        printf("%d -> ", node->val);
        if (node->next_i == -1) {
            printf("NULL");
            break;
        }
        node = &nodes[node->next_i];
    }
}
