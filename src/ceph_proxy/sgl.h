#ifndef _SGL_HEAD_H_
#define _SGL_HEAD_H_

#include <stdint.h>
#include <stddef.h>

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

typedef struct 
{
    char *buf;
    void *pageCtrl;
    uint32_t len;
    uint32_t pad;
} SGL_ENTRY_S;

#define ENTRY_PER_SGL 64
typedef struct tagSGL_S
{
   struct tagSGL_S *nextSgl;
   uint16_t  entrySumInChain;
   uint16_t  entrySumInSgl;
   uint32_t  flag;
   uint64_t  serialNum;
   SGL_ENTRY_S entrys[ENTRY_PER_SGL];
   struct list_head stSglNode;
   uint32_t  cpuid;
} SGL_S;

#endif

   

