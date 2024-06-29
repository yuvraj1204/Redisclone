#include <string.h>
#include <iostream>

#ifndef HASHTABLE_H
#define HASHTABLE_H

class HNode
{
public:
    HNode *next;
    u_int64_t hcode;

    HNode()
    {
        next = NULL;
        hcode = 0;
    }
};

class HTab
{
public:
    HNode **tab;
    size_t mask;
    size_t slots;
    size_t size;

    HTab()
    {
        tab = NULL;
        mask = slots = size = 0;
    }
    HTab(size_t n);

    void insert(HNode *node);
    HNode **h_lookup(HNode *key, bool (*cmp)(HNode *, HNode *));
    HNode *h_detach(HNode **from);
    void h_scan(void (*f)(HNode *, void *), void *arg);
};

class HMap
{
public:
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;

    HNode *hm_lookup(HNode *key, bool (*cmp)(HNode *, HNode *));
    void hm_insert(HNode *node);
    HNode *hm_pop(HNode *key, bool (*cmp)(HNode *, HNode *));
    size_t hm_size();

private:
    void hm_help_resizing();
    void hm_start_resizing();
};

#endif
