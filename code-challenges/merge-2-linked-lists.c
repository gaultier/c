#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct item_t {
    int val;
    int i;
    int next_i;
};
typedef struct item_t item_t;

void merge_sorted_linked_lists_do(item_t* nodes, item_t* lesser,
                                  item_t* greater) {
    if (lesser == NULL || greater == NULL) return;

    // Swap if passed the wrong way around
    if (lesser->val > greater->val) {
        item_t* tmp = lesser;
        lesser = greater;
        greater = tmp;
    }

    item_t* lesser_next = lesser->next_i == -1 ? NULL : &nodes[lesser->next_i];
    // Merge greater into lesser if it is suitable i.e. lesser->val <
    // greater->val < lesser_next->val, with NULL checks
    if (lesser_next == NULL ||
        (lesser_next != NULL && lesser_next->val > greater->val)) {
        int greater_next_i = greater->next_i;
        greater->next_i = lesser->next_i;
        lesser->next_i = greater->i;

        // `greater` now points to the next item in the `greater` list,
        // since we removed its head, or NULL if it is now empty
        if (greater_next_i == -1)
            greater = NULL;
        else
            greater = &nodes[greater_next_i];

    } else if (lesser_next != NULL && lesser_next->val <= greater->val) {
        lesser = lesser_next;
    }
    merge_sorted_linked_lists_do(nodes, lesser, greater);
}

void merge_sorted_linked_lists(item_t* nodes, item_t* lesser, item_t* greater,
                               item_t** head) {
    assert(head != NULL);
    if (lesser == NULL || greater == NULL) return;

    // Swap if passed the wrong way around
    if (lesser->val > greater->val) {
        item_t* tmp = lesser;
        lesser = greater;
        greater = tmp;
    }
    *head = lesser;

    merge_sorted_linked_lists_do(nodes, lesser, greater);
}

int main() {
    item_t nodes[] = {
        [0] = {.val = 19, .i = 0, .next_i = -1},
        [1] = {.val = 15, .i = 1, .next_i = 0},
        [2] = {.val = 8, .i = 2, .next_i = 1},
        [3] = {.val = 4, .i = 3, .next_i = 2},
        [4] = {.val = 16, .i = 4, .next_i = -1},
        [5] = {.val = 10, .i = 5, .next_i = 4},
        [6] = {.val = 9, .i = 6, .next_i = 5},
        [7] = {.val = 7, .i = 7, .next_i = 6},
    };
    item_t* head1 = &nodes[3];
    item_t* head2 = &nodes[7];

    item_t* head = NULL;
    merge_sorted_linked_lists(nodes, head1, head2, &head);

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
