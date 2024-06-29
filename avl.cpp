#include <iostream>
#include <algorithm>
#include "avl.h"

void avl_init(AVLNode *node)
{
    node->depth = 1;
    node->cnt = 1;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
}
uint32_t avl_depth(AVLNode *node)
{
    return node ? node->depth : 0;
}

uint32_t avl_cnt(AVLNode *node)
{
    return node ? node->cnt : 0;
}

void avl_update(AVLNode *node)
{
    node->depth = 1 + std::max(avl_depth(node->left), avl_depth(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

AVLNode *rotate_left(AVLNode *root)
{
    AVLNode *new_root = root->right;
    if (new_root->left)
    {
        new_root->left->parent = root;
    }
    root->right = new_root->left;
    new_root->left = root;

    new_root->parent = root->parent;
    root->parent = new_root;

    avl_update(root);
    avl_update(new_root);
    return new_root;
}

AVLNode *rotate_right(AVLNode *root)
{
    AVLNode *new_root = root->left;
    if (new_root->right)
    {
        new_root->right->parent = root;
    }
    root->left = new_root->right;
    new_root->right = root;

    new_root->parent = root->parent;
    root->parent = new_root;

    avl_update(root);
    avl_update(new_root);
    return new_root;
}

AVLNode *avl_fix_left(AVLNode *root)
{
    if (avl_depth(root->left->left) < avl_depth(root->left->right))
    {
        root->left = rotate_left(root->left);
    }
    return rotate_right(root);
}

AVLNode *avl_fix_right(AVLNode *root)
{
    if (avl_depth(root->right->right) < avl_depth(root->right->left))
    {
        root->right = rotate_left(root->right);
    }
    return rotate_left(root);
}

AVLNode *avl_fix(AVLNode *node)
{
    while (true)
    {
        avl_update(node);
        uint32_t l = avl_depth(node->left);
        uint32_t r = avl_depth(node->right);

        AVLNode **from = NULL;
        if (node->parent)
        {
            from = (node->parent->left == node) ? &node : &node->parent->right;
        }

        if (l == r + 2)
        {
            node = avl_fix_left(node);
        }
        else if (r == l + 2)
        {
            node = avl_fix_right(node);
        }

        if (!from)
        {
            return node;
        }
        *from = node;
        node = node->parent;
    }
}

AVLNode *avl_del(AVLNode *node)
{
    if (node->right == NULL)
    {
        AVLNode *parent = node->parent;
        if (node->left)
        {
            node->left->parent = parent;
        }
        if (parent)
        {
            (parent->left == node ? parent->left : parent->right) = node->left;
            return avl_fix(parent);
        }
        else
        {
            return node->left;
        }
    }
    else
    {
        AVLNode *victim = node->right;
        while (victim->left)
        {
            victim = victim->left;
        }
        AVLNode *root = avl_del(victim);

        *victim = *node;
        if (victim->left)
        {
            victim->left->parent = victim;
        }
        if (victim->right)
        {
            victim->right->parent = victim;
        }
        AVLNode *parent = node->parent;
        if (parent)
        {
            (parent->left == node ? parent->left : parent->right) = victim;
            return root;
        }
        else
        {
            return victim;
        }
    }
}
