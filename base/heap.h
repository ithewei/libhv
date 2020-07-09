#ifndef HV_HEAP_H_
#define HV_HEAP_H_

#include <assert.h> // for assert
#include <stddef.h> // for NULL

struct heap_node {
    struct heap_node* parent;
    struct heap_node* left;
    struct heap_node* right;
};

typedef int (*heap_compare_fn)(const struct heap_node* lhs, const struct heap_node* rhs);
struct heap {
    struct heap_node* root;
    int nelts;
    // if compare is less_than, root is min of heap
    // if compare is larger_than, root is max of heap
    heap_compare_fn compare;
};

static inline void heap_init(struct heap* heap, heap_compare_fn fn) {
    heap->root = NULL;
    heap->nelts = 0;
    heap->compare = fn;
}

// replace s with r
static inline void heap_replace(struct heap* heap, struct heap_node* s, struct heap_node* r) {
    // s->parent->child, s->left->parent, s->right->parent
    if (s->parent == NULL) heap->root = r;
    else if (s->parent->left == s) s->parent->left = r;
    else if (s->parent->right == s) s->parent->right = r;

    if (s->left) s->left->parent = r;
    if (s->right) s->right->parent = r;
    if (r) {
        //*r = *s;
        r->parent = s->parent;
        r->left = s->left;
        r->right = s->right;
    }
}

static inline void heap_swap(struct heap* heap, struct heap_node* parent, struct heap_node* child) {
    assert(child->parent == parent && (parent->left == child || parent->right == child));
    struct heap_node* pparent = parent->parent;
    struct heap_node* lchild = child->left;
    struct heap_node* rchild = child->right;
    struct heap_node* sibling = NULL;

    if (pparent == NULL) heap->root = child;
    else if (pparent->left == parent) pparent->left = child;
    else if (pparent->right == parent) pparent->right = child;

    if (lchild) lchild->parent = parent;
    if (rchild) rchild->parent = parent;

    child->parent  = pparent;
    if (parent->left == child) {
        sibling = parent->right;
        child->left = parent;
        child->right = sibling;
    } else {
        sibling = parent->left;
        child->left = sibling;
        child->right = parent;
    }
    if (sibling) sibling->parent = child;

    parent->parent = child;
    parent->left   = lchild;
    parent->right  = rchild;
}

static inline void heap_insert(struct heap* heap, struct heap_node* node) {
    // get last => insert node => sift up
    // 0: left, 1: right
    int path = 0;
    int n,d;
    ++heap->nelts;
    // traverse from bottom to up, get path of last node
    for (d = 0, n = heap->nelts; n >= 2; ++d, n>>=1) {
        path = (path << 1) | (n & 1);
    }

    // get last->parent by path
    struct heap_node* parent = heap->root;
    while(d > 1) {
        parent = (path & 1) ? parent->right : parent->left;
        --d;
        path >>= 1;
    }

    // insert node
    node->parent = parent;
    if (parent == NULL) heap->root = node;
    else if (path & 1) parent->right = node;
    else parent->left = node;

    // sift up
    if (heap->compare) {
        while (node->parent && heap->compare(node, node->parent)) {
            heap_swap(heap, node->parent, node);
        }
    }
}

static inline void heap_remove(struct heap* heap, struct heap_node* node) {
    if (heap->nelts == 0)   return;
    // get last => replace node with last => sift down and sift up
    // 0: left, 1: right
    int path = 0;
    int n,d;
    // traverse from bottom to up, get path of last node
    for (d = 0, n = heap->nelts; n >= 2; ++d, n>>=1) {
        path = (path << 1) | (n & 1);
    }
    --heap->nelts;

    // get last->parent by path
    struct heap_node* parent = heap->root;
    while(d > 1) {
        parent = (path & 1) ? parent->right : parent->left;
        --d;
        path >>= 1;
    }

    // replace node with last
    struct heap_node* last = NULL;
    if (parent == NULL) {
        return;
    }
    else if (path & 1) {
        last = parent->right;
        parent->right = NULL;
    }
    else {
        last = parent->left;
        parent->left = NULL;
    }
    if (last == NULL) {
        if (heap->root == node) {
            heap->root = NULL;
        }
        return;
    }
    heap_replace(heap, node, last);
    node->parent = node->left = node->right = NULL;

    if (!heap->compare) return;
    struct heap_node* v = last;
    struct heap_node* est = NULL;
    // sift down
    while (1) {
        est = v;
        if (v->left) est = heap->compare(est, v->left) ? est : v->left;
        if (v->right) est = heap->compare(est, v->right) ? est : v->right;
        if (est == v) break;
        heap_swap(heap, v, est);
    }
    // sift up
    while (v->parent && heap->compare(v, v->parent)) {
        heap_swap(heap, v->parent, v);
    }
}

static inline void heap_dequeue(struct heap* heap) {
    heap_remove(heap, heap->root);
}

#endif // HV_HEAP_H_
