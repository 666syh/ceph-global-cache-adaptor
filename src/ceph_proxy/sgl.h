/*
* Copyright (c) 2021 Huawei Technologies Co., Ltd All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*	http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

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

   

