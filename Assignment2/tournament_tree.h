#pragma once

#include "kthread.h"

typedef struct trnmnt_tree {
    int depth; // the tree depth 
    int *mutexes;     
    enum mutex_state *threads_state; // which threads ID acquire the tree 
} trnmnt_tree;

trnmnt_tree* trnmnt_tree_alloc(int depth);
int trnmnt_tree_dealloc(trnmnt_tree* tree);
int trnmnt_tree_acquire(trnmnt_tree* tree,int ID);
int trnmnt_tree_release(trnmnt_tree* tree,int ID);