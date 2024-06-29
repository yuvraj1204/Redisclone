struct AVLNode
{
    uint32_t depth;
    uint32_t cnt;
    AVLNode *left = NULL;
    AVLNode *right = NULL;
    AVLNode *parent = NULL;
};

void avl_init(AVLNode *node);
uint32_t avl_depth(AVLNode *node);
uint32_t avl_cnt(AVLNode *node);
void avl_update(AVLNode *node);

AVLNode *rotate_left(AVLNode *root);
AVLNode *rotate_right(AVLNode *root);
AVLNode *avl_fix_left(AVLNode *root);
AVLNode *avl_fix_right(AVLNode *root);
AVLNode *avl_fix(AVLNode *node);

AVLNode *avl_del(AVLNode *node);
