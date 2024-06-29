#include <assert.h>
#include "hashtable.h"

HTab::HTab(size_t n)
{
    assert(n > 0 && ((n - 1) & n) == 0);
    tab = new HNode *[n];
    for (size_t i = 0; i < n; i++)
    {
        tab[i] = NULL;
    }
    mask = n - 1;
    size = 0;
    slots = n;
}

void HTab::insert(HNode *node)
{
    // Constant time insertion
    size_t pos = node->hcode & mask;
    HNode *next_node = tab[pos];
    node->next = next_node;
    tab[pos] = node;
    size++;
}

HNode **HTab::h_lookup(HNode *key, bool (*cmp)(HNode *, HNode *))
{
    // Here , we return the pointer to the node pointer key
    if (!tab)
        return NULL;
    size_t pos = key->hcode & mask;

    HNode **from = &tab[pos];
    if (!from)
        return NULL;
    while (*from)
    {
        if (cmp(*from, key))
        {
            return from;
        }
        from = &((*from)->next);
    }
    return NULL;
}

HNode *HTab::h_detach(HNode **from)
{
    // This replaces the next node address at the original node address
    HNode *node = *from;
    *from = (*from)->next;
    size--;
    // Return the original node which can be used for some purpose
    return node;
}

void HTab::h_scan(void (*f)(HNode *, void *), void *arg)
{
    if (size == 0)
        return;
    for (size_t i = 0; i <= mask; i++)
    {
        HNode *node = tab[i];
        while (node)
        {
            f(node, arg);
            node = node->next;
        }
    }
}

const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;

void HMap::hm_help_resizing()
{
    if (ht2.tab == NULL)
    {
        return;
    }
    size_t nwork = 0;
    while (nwork < k_resizing_work && ht2.size > 0)
    {
        assert(resizing_pos <= ht2.mask);

        HNode **from = &ht2.tab[resizing_pos];
        if (!*from)
        {
            resizing_pos++;
            continue;
        }

        ht1.insert(ht2.h_detach(from));
        nwork++;
    }

    if (ht2.size == 0)
    {
        // Done with resizing
        free(ht2.tab);
        ht2 = HTab();
    }
}

void HMap::hm_insert(HNode *node)
{
    if (!ht1.tab)
    {
        ht1 = HTab(4);
    }
    ht1.insert(node);

    if (!ht2.tab)
    {
        size_t load_factor = ht1.size / (ht1.mask + 1);
        if (load_factor >= k_max_load_factor)
        {
            hm_start_resizing();
        }
    }
    hm_help_resizing();
}

void HMap::hm_start_resizing()
{
    assert(ht2.tab == NULL);

    ht2 = ht1;
    ht1 = HTab((ht1.mask + 1) * 2);
    resizing_pos = 0;
}

HNode *HMap::hm_lookup(HNode *key, bool (*cmp)(HNode *, HNode *))
{
    hm_help_resizing();
    HNode **from = ht1.h_lookup(key, cmp);
    if (!from)
    {
        from = ht2.h_lookup(key, cmp);
    }
    return from ? *from : NULL;
}

HNode *HMap::hm_pop(HNode *key, bool (*cmp)(HNode *, HNode *))
{
    hm_help_resizing();
    HNode **from = ht1.h_lookup(key, cmp);
    if (from)
    {
        return ht1.h_detach(from);
    }
    from = ht2.h_lookup(key, cmp);
    if (from)
    {
        return ht2.h_detach(from);
    }
    return NULL;
}

size_t HMap::hm_size()
{
    return ht1.size + ht2.size;
}
