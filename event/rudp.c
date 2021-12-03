#include "rudp.h"

#if WITH_RUDP

#include "hevent.h"

void rudp_entry_free(rudp_entry_t* entry) {
#if WITH_KCP
    kcp_release(&entry->kcp);
#endif
    HV_FREE(entry);
}

void rudp_init(rudp_t* rudp) {
    // printf("rudp init\n");
    rudp->rb_root.rb_node = NULL;
    hmutex_init(&rudp->mutex);
}

void rudp_cleanup(rudp_t* rudp) {
    // printf("rudp cleaup\n");
    struct rb_node* n = NULL;
    rudp_entry_t* e = NULL;
    while ((n = rudp->rb_root.rb_node)) {
        e = rb_entry(n, rudp_entry_t, rb_node);
        rb_erase(n, &rudp->rb_root);
        rudp_entry_free(e);
    }
    hmutex_destroy(&rudp->mutex);
}

bool rudp_insert(rudp_t* rudp, rudp_entry_t* entry) {
    struct rb_node** n = &rudp->rb_root.rb_node;
    struct rb_node* parent = NULL;
    rudp_entry_t* e = NULL;
    int cmp = 0;
    bool exists = false;
    while (*n) {
        parent = *n;
        e = rb_entry(*n, rudp_entry_t, rb_node);
        cmp = memcmp(&entry->addr, &e->addr, sizeof(sockaddr_u));
        if (cmp < 0) {
            n = &(*n)->rb_left;
        } else if (cmp > 0) {
            n = &(*n)->rb_right;
        } else {
            exists = true;
            break;
        }
    }

    if (!exists) {
        rb_link_node(&entry->rb_node, parent, n);
        rb_insert_color(&entry->rb_node, &rudp->rb_root);
    }
    return !exists;
}

rudp_entry_t* rudp_search(rudp_t* rudp, struct sockaddr* addr) {
    struct rb_node* n = rudp->rb_root.rb_node;
    rudp_entry_t* e = NULL;
    int cmp = 0;
    bool exists = false;
    while (n) {
        e = rb_entry(n, rudp_entry_t, rb_node);
        cmp = memcmp(addr, &e->addr, sizeof(sockaddr_u));
        if (cmp < 0) {
            n = n->rb_left;
        } else if (cmp > 0) {
            n = n->rb_right;
        } else {
            exists = true;
            break;
        }
    }
    return exists ? e : NULL;
}

rudp_entry_t* rudp_remove(rudp_t* rudp, struct sockaddr* addr) {
    hmutex_lock(&rudp->mutex);
    rudp_entry_t* e = rudp_search(rudp, addr);
    if (e) {
        // printf("rudp_remove ");
        // SOCKADDR_PRINT(addr);
        rb_erase(&e->rb_node, &rudp->rb_root);
    }
    hmutex_unlock(&rudp->mutex);
    return e;
}

rudp_entry_t* rudp_get(rudp_t* rudp, struct sockaddr* addr) {
    hmutex_lock(&rudp->mutex);
    struct rb_node** n = &rudp->rb_root.rb_node;
    struct rb_node* parent = NULL;
    rudp_entry_t* e = NULL;
    int cmp = 0;
    bool exists = false;
    // search
    while (*n) {
        parent = *n;
        e = rb_entry(*n, rudp_entry_t, rb_node);
        cmp = memcmp(addr, &e->addr, sizeof(sockaddr_u));
        if (cmp < 0) {
            n = &(*n)->rb_left;
        } else if (cmp > 0) {
            n = &(*n)->rb_right;
        } else {
            exists = true;
            break;
        }
    }

    if (!exists) {
        // insert
        // printf("rudp_insert ");
        // SOCKADDR_PRINT(addr);
        HV_ALLOC_SIZEOF(e);
        memcpy(&e->addr, addr, SOCKADDR_LEN(addr));
        rb_link_node(&e->rb_node, parent, n);
        rb_insert_color(&e->rb_node, &rudp->rb_root);
    }
    hmutex_unlock(&rudp->mutex);
    return e;
}

void rudp_del(rudp_t* rudp, struct sockaddr* addr) {
    hmutex_lock(&rudp->mutex);
    rudp_entry_t* e = rudp_search(rudp, addr);
    if (e) {
        // printf("rudp_remove ");
        // SOCKADDR_PRINT(addr);
        rb_erase(&e->rb_node, &rudp->rb_root);
        rudp_entry_free(e);
    }
    hmutex_unlock(&rudp->mutex);
}

rudp_entry_t* hio_get_rudp(hio_t* io) {
    rudp_entry_t* rudp = rudp_get(&io->rudp, io->peeraddr);
    rudp->io = io;
    return rudp;
}

static void hio_close_rudp_event_cb(hevent_t* ev) {
    rudp_entry_t* entry = (rudp_entry_t*)ev->userdata;
    rudp_del(&entry->io->rudp, (struct sockaddr*)&entry->addr);
    // rudp_entry_free(entry);
}

int hio_close_rudp(hio_t* io, struct sockaddr* peeraddr) {
    if (peeraddr == NULL) peeraddr = io->peeraddr;
    // NOTE: do rudp_del for thread-safe
    rudp_entry_t* entry = rudp_get(&io->rudp, peeraddr);
    // NOTE: just rudp_remove first, do rudp_entry_free async for safe.
    // rudp_entry_t* entry = rudp_remove(&io->rudp, peeraddr);
    if (entry) {
        hevent_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.cb = hio_close_rudp_event_cb;
        ev.userdata = entry;
        ev.priority = HEVENT_HIGH_PRIORITY;
        hloop_post_event(io->loop, &ev);
    }
    return 0;
}

#endif
