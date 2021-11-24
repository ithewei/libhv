#include <stdio.h>
#include <string.h>

#include "rbtree.h"

typedef int rbtree_key_type;
typedef int rbtree_val_type;

struct rbtree_entry {
    struct rb_node  rb_node;
    rbtree_key_type key;
    rbtree_val_type val;
};

int rbtree_insert(struct rb_root* root, struct rbtree_entry* entry) {
    printf("insert %d\n", entry->key);
    struct rb_node** n = &root->rb_node;
    struct rb_node* parent = NULL;
    struct rbtree_entry* e = NULL;
    while (*n) {
        parent = *n;
        e = rb_entry(*n, struct rbtree_entry, rb_node);
        if (entry->key < e->key) {
            n = &(*n)->rb_left;
        } else if (entry->key > e->key) {
            n = &(*n)->rb_right;
        } else {
            return -1;
        }
    }

    rb_link_node(&entry->rb_node, parent, n);
    rb_insert_color(&entry->rb_node, root);
    return 0;
}

int rbtree_remove(struct rb_root* root, struct rbtree_entry* entry) {
    printf("remove %d\n", entry->key);
    rb_erase(&entry->rb_node, root);
    return 0;
}

struct rbtree_entry* rbtree_search(struct rb_root* root, const rbtree_key_type* key) {
    struct rb_node* n = root->rb_node;
    struct rbtree_entry* e = NULL;
    while (n) {
        e = rb_entry(n, struct rbtree_entry, rb_node);
        if (*key < e->key) {
            n = n->rb_left;
        } else if (*key > e->key) {
            n = n->rb_right;
        } else {
            return e;
        }
    }
    return NULL;
}

void rbtree_entry_print(struct rbtree_entry* entry) {
    if (entry == NULL) {
        printf("null\n");
        return;
    }
    printf("%d:%d\n", entry->key, entry->val);
}

int main() {
    struct rb_root root = { NULL };
    struct rbtree_entry* entry = NULL;

    struct rbtree_entry entries[10];
    for (int i = 0; i < 10; ++i) {
        memset(&entries[i], 0, sizeof(struct rbtree_entry));
        entries[i].key = i;
        entries[i].val = i;
    }

    rbtree_insert(&root, &entries[1]);
    rbtree_insert(&root, &entries[2]);
    rbtree_insert(&root, &entries[3]);
    rbtree_insert(&root, &entries[7]);
    rbtree_insert(&root, &entries[8]);
    rbtree_insert(&root, &entries[9]);
    rbtree_insert(&root, &entries[4]);
    rbtree_insert(&root, &entries[5]);
    rbtree_insert(&root, &entries[6]);

    rbtree_remove(&root, &entries[1]);
    rbtree_remove(&root, &entries[9]);
    rbtree_remove(&root, &entries[4]);
    rbtree_remove(&root, &entries[6]);

    int key = 5;
    entry = rbtree_search(&root, &key);
    rbtree_entry_print(entry);

    key = 4;
    entry = rbtree_search(&root, &key);
    rbtree_entry_print(entry);

    struct rb_node* node = NULL;
    // while((node = rb_first(&root))) {
    while((node = root.rb_node)) {
        entry = rb_entry(node, struct rbtree_entry, rb_node);
        rb_erase(node, &root);
        rbtree_entry_print(entry);
        memset(entry, 0, sizeof(struct rbtree_entry));
    }

    return 0;
}
