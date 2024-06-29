#include <iostream>
#include <assert.h>
#include <set>

#include "avl.h"

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0 ) -> member)* __mptr =  (ptr); \
    (type * ) ((char *)__mptr - offsetof(type, member)); })

struct Data
{
    AVLNode node;
    uint32_t val;
    Data() { val = 0; }
};

struct Container
{
    AVLNode *root;
    Container() { root = NULL; }
};

void add(Container &c, uint32_t val)
{
    Data *data = new Data();
    avl_init(&data->node);
    data->val = val;

    if (!c.root)
    {
        c.root = &(data->node);
        return;
    }

    AVLNode *curr = c.root;
    while (true)
    {
        AVLNode **from = (val < container_of(curr, Data, node)->val) ? &curr->left : &curr->right;
        if (!*from)
        {
            *from = &data->node;
            data->node.parent = curr;
            c.root = avl_fix(&data->node);
            break;
        }
        curr = *from;
    }
}

bool del(Container &c, uint32_t val)
{
    AVLNode *curr = c.root;
    while (curr)
    {
        uint32_t node_val = container_of(curr, Data, node)->val;
        if (val == node_val)
        {
            break;
        }
        curr = val < node_val ? curr->left : curr->right;
    }
    if (!curr)
    {
        return false;
    }
    c.root = avl_del(curr);
    delete container_of(curr, Data, node);
    return true;
}

void avl_verify(AVLNode *parent, AVLNode *node)
{
    if (!node)
        return;

    assert(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);

    std::cout << parent << " " << ((parent) ? container_of(parent, Data, node)->val : 9191) << " | " << node << " " << container_of(node, Data, node)->val << std::endl;

    assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));

    uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right);
    assert(l == r || l + 1 == r || r + 1 == l);
    assert(node->depth == 1 + std::max(l, r));

    uint32_t val = container_of(node, Data, node)->val;
    if (node->left)
    {
        assert(node->left->parent == node);
        assert(container_of(node->left, Data, node)->val <= val);
    }

    if (node->right)
    {
        assert(node->right->parent == node);
        assert(container_of(node->right, Data, node)->val >= val);
    }
    // std::cout << std::endl;
}

void extract(AVLNode *node, std::multiset<uint32_t> &extracted)
{
    if (!node)
        return;

    extract(node->left, extracted);
    extracted.insert(container_of(node, Data, node)->val);
    extract(node->right, extracted);
}

void container_verify(Container &c, const std::multiset<uint32_t> &ref)
{
    avl_verify(NULL, c.root);
    assert(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract(c.root, extracted);
    assert(extracted == ref);
}

void dispose(Container &c)
{
    while (c.root)
    {
        AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
    }
}

int main()
{
    Container c;
    container_verify(c, {});
    add(c, 123);
    assert(!del(c, 124));
    assert(del(c, 123));

    container_verify(c, {});

    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < 1000; i += 3)
    {
        add(c, i);
        ref.insert(i);
        container_verify(c, ref);
    }

    return 0;
}
