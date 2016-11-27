#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Driver.h"
#include "defs.h"
#include "Queue_LinkedList.h"
#include "uthash.h"


#define NUM_INODE_BLOCKS 16
#define NUM_DIRECT 13

typedef struct inode {
    int direct[NUM_DIRECT];
    int singl;
    int doubl;
    int tripl;
} inode_t;


struct file {
    char *filename;     /* hash key */
    int size;
    int block;
    UT_hash_handle hh;  /* makes this structure hashable by uthash */
} file;

struct file *files = NULL;  /* hashmap, must be initialized to NULL for uthash */
queue freequeue = {NULL, NULL}; 


void del_block(int block){
    enqueue(&freequeue, block);
}

int del(int block, int layer){
    int block_buf[BYTES_PER_SECTOR];
    int i;

    if(!DevRead(block, (char *)&block_buf)){
        return 0;
    }
   
    for(i = 0; i < NUM_INODE_BLOCKS; i++){
        if(block_buf[i] != -1){
            if(layer == 1){
                del_block(block_buf[i]);
            } else{
                del(block_buf[i], layer - 1);
            }
        }
    }
    
    return 1;
}


int CSCI460_Delete(	char *Filename){
    struct file *f;
    inode_t inode;
    int i;

    //hash Filename to get the file
    HASH_FIND_STR(files, Filename, f);

    if(!f){
        return 0;
    }

    DevRead(f->block, (char *) &inode);

    for(i = 0; i < NUM_DIRECT; i++){
        if(inode.direct[i] != -1){
            del_block(inode.direct[i]);
        } else{
            break;
        }
    }

    if(inode.singl != -1){
        del(inode.singl, 1);
    }
    if(inode.doubl != -1){
        del(inode.doubl, 2);
    }
    if(inode.tripl != -1){
        del(inode.tripl, 3);
    }

    //delete hash from hashmap
    HASH_DEL(files, f);

    free(f);
    return 1;
}

int CSCI460_Format (){
    struct file *f;
    struct file *tmp;
    int i;

    //All hash associations should be cleared
    HASH_ITER(hh, files, f, tmp){
        HASH_DEL(files, f);
        free(f);
    } 
    
    while(!empty(&freequeue)){
        dequeue(&freequeue);   
    }

    for(i = 0; i < SECTORS; i++){
        enqueue(&freequeue, i); 
    }

    DevFormat();
    return 1;
}


int write_block(char *buf, int *pos, int size, int *block){
    int free_block;
    char loc_buf[BYTES_PER_SECTOR];

    if(empty(&freequeue)){
        return 0;
    }

    free_block = front(&freequeue);

    bzero(loc_buf, BYTES_PER_SECTOR);

    if(*pos + BYTES_PER_SECTOR > size){
       memcpy(loc_buf, &buf[*pos], strlen(&buf[*pos])); 
       *pos += strlen(&buf[*pos]);
    } else{
       memcpy(loc_buf, &buf[*pos], BYTES_PER_SECTOR);
       *pos += BYTES_PER_SECTOR;
    }

    if (!DevWrite(free_block, loc_buf)){
        return 0;
    }
    dequeue(&freequeue);
    *block = free_block;
    return 1;
}

int write(int *block, char *buf, int *pos, int size, int layer){
    int i;
    int tmp = 0;

    if(layer == 0){
        if (!write_block(buf, pos, size, block)){
            return 0;
        }
    } else{     /*Need to use an indirect block */
        int ind[NUM_INODE_BLOCKS];
        for(i = 0; i < NUM_INODE_BLOCKS; i++){
            ind[i] = -1;
        }
        for(i = 0; i < NUM_INODE_BLOCKS; i++){
            if(*pos < size){
                if(!write(&ind[i], buf, pos, size, layer - 1)){
                    return 0;
                }
            } else{
                break;
            }
        }
        write_block((char *)&ind, &tmp, sizeof(ind), block); 
    }
    return 1; 
}


struct file* create_file(char *FileName, int Size){
    struct file *f = malloc(sizeof(struct file));
    inode_t inode;
    int i;
    int pos = 0;

    f->filename = FileName;
    f->size = Size;

    for(i = 0; i < NUM_DIRECT; i++){
        inode.direct[i] = -1;
    }
    inode.singl = -1;
    inode.doubl = -1;
    inode.tripl = -1;
    

    HASH_ADD_KEYPTR(hh, files, f->filename, strlen(f->filename), f);
    if(!write_block((char *) &inode, &pos, sizeof(inode), &(f->block))){
        perror("Couldn't write an inode to disk.");
    }
    
    return f;
}

int CSCI460_Write (	char *FileName, int Size, char *Data){
    struct file *f;
    int pos = 0;
    int i = 0;
    inode_t inode;

    HASH_FIND_STR(files, FileName, f);

    if(f){      /* Delete File if it exists */
        CSCI460_Delete(FileName);
    }

    //Create new file
    f = create_file(FileName, Size);
    DevRead(f->block, (char *)&inode);

    for(i = 0; i < NUM_DIRECT; i++){
        if(pos < Size - 1){
            if(!write_block(Data, &pos, Size, &inode.direct[i])){
                return 0;
            }
        } else{
            break;
        }
    }

    if(pos < Size -1){
        if(!write(&inode.singl, Data, &pos, Size, 1)){
            return 0;
        }
    }
    if(pos < Size -1){
        if(!write(&inode.doubl, Data, &pos, Size, 2)){
            return 0;
        }
    }
    if(pos < Size -1){
        if(!write(&inode.tripl, Data, &pos, Size, 3)){
            return 0;
        }
    }
    DevWrite(f->block, (char *)&inode);
    return 1;
}

int read_block(int block, char *buf, int max, int *pos){
    char loc_buf[BYTES_PER_SECTOR];

    if(block != -1){
        if(!DevRead(block, loc_buf)){
            return 0;
        }
        
        if((*pos + BYTES_PER_SECTOR) <= max){
           memcpy(&buf[*pos], loc_buf, BYTES_PER_SECTOR); 
           *pos += BYTES_PER_SECTOR;
        }
        else {
           memcpy(&buf[*pos], loc_buf, max - *pos + 1);
           *pos += (max - *pos + 1);
        }
    }
    return 1;
}

int iread(int block, char *buf, int max, int *pos, int layer){
    int block_buf[BYTES_PER_SECTOR];
    int i;

    if(!DevRead(block,(char *)&block_buf)){
        return 0;
    }

    for(i = 0; i < NUM_INODE_BLOCKS; i++){
        if(block_buf[i] != -1){
            if(layer == 1){
                read_block(block_buf[i], buf, max, pos);
            } else{
                iread(block_buf[i], buf, max, pos, layer -1);
            }
        }
    }
    return 1; 
}



int CSCI460_Read (	char *FileName, int MaxSize, char *Data){
    struct file *f;
    inode_t inode;
    int num_read = 0;
    int i;

    //hash FileName
    HASH_FIND_STR(files, FileName, f);

    if (!f){
        return 0;
    }

    //get the inode data for the file and copy into the inode
    DevRead(f->block, (char *)&inode);
   
    //read direct blocks
    for(i = 0; i < NUM_DIRECT; i++){
        if(inode.direct[i] != -1){
            read_block(inode.direct[i], Data, MaxSize, &num_read);
        } else {
            break;
        }
    }
    
    if(inode.singl != -1){
        iread(inode.singl, Data, MaxSize, &num_read, 1);
    }
    if(inode.doubl != -1){
        iread(inode.doubl, Data, MaxSize, &num_read, 2);
    }
    if(inode.tripl != -1){
        iread(inode.tripl, Data, MaxSize, &num_read, 3);
    }
    
    Data[MaxSize] = 0;
    return num_read;
}
