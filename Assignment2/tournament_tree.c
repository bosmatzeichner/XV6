#include "types.h"
#include "tournament_tree.h"
#include "user.h"


trnmnt_tree* 
trnmnt_tree_alloc(int depth){
    trnmnt_tree * tree = ((trnmnt_tree *) malloc(sizeof(trnmnt_tree)));
    tree->depth = depth;
    int tree_size =  (1 << depth)-1;
    tree->mutexes = malloc((tree_size)*sizeof(int));
    tree->threads_state = malloc((tree_size+1)*sizeof(int));
    tree->threads_state[tree_size] = EMPTY;
    int index=0;
    while (tree_size >index){    
        tree->mutexes[index] = kthread_mutex_alloc();       
        tree->threads_state[index] = EMPTY; 
        index++;       
    }       
    return tree;
}

int
trnmnt_tree_dealloc(trnmnt_tree* tree){     
    int tree_size =  (1 << tree->depth)-1;
    int success;
    if(tree->threads_state[tree_size] == USED){  
        return -1;
    }
    int index=0;
    while (tree_size > index ){          
        success = kthread_mutex_dealloc(tree->mutexes[index]);        
        if (-1 == success || tree->threads_state[index] == USED){          
            return -1;
        }
        index++;
    }
    free(tree->threads_state);
    free(tree->mutexes);
    free(tree);
    return 0;
}
int get_parent_in_tree(int index){
    return (index-1)/2;
}

int
trnmnt_tree_acquire(trnmnt_tree* tree,int ID){
    if(tree->threads_state[ID] != EMPTY){       
        return -1;
    }
    tree->threads_state[ID] = USED;
    int tree_size =  (1 << tree->depth) -1;

    int mutex_maping_index = tree_size + ID;   
    int j;

    for(j = get_parent_in_tree(mutex_maping_index); j > 0; j = get_parent_in_tree(j)){
        kthread_mutex_lock(tree->mutexes[j]);
    }
    // for final lock
    kthread_mutex_lock(tree->mutexes[j]);
    return 0;
}

int recursive_tree_release(int index, trnmnt_tree* tree){
  int result = 0;
  if(index != 0){
      result = recursive_tree_release(get_parent_in_tree(index),tree);
  }
  if (0 == result){
    result = kthread_mutex_unlock(tree->mutexes[index]);
  }
  return result;
}

int 
trnmnt_tree_release(trnmnt_tree* tree,int ID){
    if(tree->threads_state[ID] != USED){
        return -1;
    }
    tree->threads_state[ID] = EMPTY;

    int tree_size =  (1 << tree->depth) -1;

    int mutex_maping_index = tree_size + ID;    
    return recursive_tree_release(get_parent_in_tree(mutex_maping_index), tree);  
}