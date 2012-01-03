/*
 * Copyright (c) 2009      The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#ifndef LIFO_H_HAS_BEEN_INCLUDED
#define LIFO_H_HAS_BEEN_INCLUDED

#include "dague_config.h"
#include <stdlib.h>

#include "atomic.h"
#include "list_item.h"

#if defined(DAGUE_LIFO_USE_LOCKS)

#define DAGUE_LIFO_ELT_ALLOC( elt, truesize )           \
    do {                                                \
        (elt) = (__typeof__(elt))malloc( truesize );      \
    } while (0)
#define DAGUE_LIFO_ELT_FREE( elt ) free(elt)

typedef struct dague_atomic_lifo_t {
    volatile dague_list_item_t* lifo_head;
    dague_list_item_t  lifo_ghost;
    volatile uint32_t  lifo_lock;
} dague_atomic_lifo_t;

static inline int dague_atomic_lifo_is_empty( dague_atomic_lifo_t* lifo )
{
    int ret;
    dague_atomic_lock( &lifo->lifo_lock );
    ret = (lifo->lifo_head == &(lifo->lifo_ghost) ? 1 : 0);
    dague_atomic_unlock( &lifo->lifo_lock );
    return ret;
}

/* Add double-linked element to the LIFO. We will return the last head
 * of the list to allow the upper level to detect if this element is
 * the first one in the list (if the list was empty before this
 * operation).
 */
static inline void dague_atomic_lifo_push( dague_atomic_lifo_t* lifo,
                                           dague_list_item_t* items )
{
    dague_list_item_t* tail = (dague_list_item_t*)items->list_prev;

    DAGUE_ATTACH_ELEMS(lifo, items);

    dague_atomic_lock( &lifo->lifo_lock );
    tail->list_next = (dague_list_item_t*)lifo->lifo_head;
    lifo->lifo_head = items;
    dague_atomic_unlock( &lifo->lifo_lock );
}

/* Retrieve one element from the LIFO. If we reach the ghost element then the LIFO
 * is empty so we return NULL.
 */
static inline dague_list_item_t* dague_atomic_lifo_pop( dague_atomic_lifo_t* lifo )
{
    dague_list_item_t* item;

    dague_atomic_lock( &lifo->lifo_lock );
    item = (dague_list_item_t*)lifo->lifo_head;
    lifo->lifo_head = item->list_next;
    dague_atomic_unlock( &lifo->lifo_lock );

    if( item == &(lifo->lifo_ghost) ) 
        return NULL;

    DAGUE_DETACH_ELEM(item);
    return item;
}

static inline void dague_atomic_lifo_construct( dague_atomic_lifo_t* lifo )
{
    lifo->lifo_ghost.list_next = &(lifo->lifo_ghost);
    lifo->lifo_head = &(lifo->lifo_ghost);
    lifo->lifo_lock = 0;
}

static inline void dague_atomic_lifo_destruct( dague_atomic_lifo_t *lifo )
{
    (void)lifo;
}

#else


#define DAGUE_LIFO_ALIGNMENT_BITS  3
#define DAGUE_LIFO_ALIGNMENT      (1 << DAGUE_LIFO_ALIGNMENT_BITS )
#define DAGUE_LIFO_CNTMASK        (DAGUE_LIFO_ALIGNMENT-1)
#define DAGUE_LIFO_PTRMASK        (~(DAGUE_LIFO_CNTMASK))
#define DAGUE_LIFO_CNT( v )       ( (uintptr_t) ( (uintptr_t)(v) & DAGUE_LIFO_CNTMASK ) )
#define DAGUE_LIFO_PTR( v )       ( (dague_list_item_t *) ( (uintptr_t)(v) & DAGUE_LIFO_PTRMASK ) )
#define DAGUE_LIFO_VAL( p, c)     ( (dague_list_item_t *) ( ((uintptr_t)DAGUE_LIFO_PTR(p)) | DAGUE_LIFO_CNT(c) ) )

#define DAGUE_LIFO_ELT_ALLOC( elt, truesize )                           \
    do {                                                                \
        dague_list_item_t *_elt;                                        \
        (void)posix_memalign( (void**)&_elt, DAGUE_LIFO_ALIGNMENT, (truesize) ); \
        _elt->keeper_of_the_seven_keys = 0;                             \
        (elt) = (__typeof__(elt))_elt;                                                   \
    } while (0)
#define DAGUE_LIFO_ELT_FREE( elt ) free(elt)

typedef struct dague_atomic_lifo_t {
    dague_list_item_t *lifo_head;
    dague_list_item_t *lifo_ghost;
} dague_atomic_lifo_t;

/* The ghost pointer will never change. The head will change via an
 * atomic compare-and-swap. On most architectures the reading of a
 * pointer is an atomic operation so we don't have to protect it.
 */
static inline int dague_atomic_lifo_is_empty( dague_atomic_lifo_t* lifo )
{
    return ( (DAGUE_LIFO_PTR(lifo->lifo_head) == lifo->lifo_ghost) ? 1 : 0);
}

/* Add double-linked element to the LIFO. We will return the last head
 * of the list to allow the upper level to detect if this element is
 * the first one in the list (if the list was empty before this
 * operation).
 */
static inline void dague_atomic_lifo_push( dague_atomic_lifo_t* lifo,
                                           dague_list_item_t* items )
{
    dague_list_item_t* tail = (dague_list_item_t*)items->list_prev;
    dague_list_item_t* tp;
#if defined(DAGUE_DEBUG)
    assert(  (uintptr_t)items % DAGUE_LIFO_ALIGNMENT == 0 );
#endif

    DAGUE_ATTACH_ELEMS(lifo, items);
    tp = DAGUE_LIFO_VAL( items, (items->keeper_of_the_seven_keys + 1) );
    do {
        tail->list_next = lifo->lifo_head;
        if( dague_atomic_cas( &(lifo->lifo_head),
                              (uintptr_t) tail->list_next,
                              (uintptr_t) tp ) ) {
            return;
        }
        /* DO some kind of pause to release the bus */
    } while( 1 );
}

/* Retrieve one element from the LIFO. If we reach the ghost element then the LIFO
 * is empty so we return NULL.
 */
static inline dague_list_item_t* dague_atomic_lifo_pop( dague_atomic_lifo_t* lifo )
{
    dague_list_item_t *item, *save;

    while(DAGUE_LIFO_PTR((item = lifo->lifo_head)) != lifo->lifo_ghost) {
        if( dague_atomic_cas( &(lifo->lifo_head),
                              (uintptr_t) item,
                              (uintptr_t) DAGUE_LIFO_PTR(item)->list_next ) )
            break;
        /* Do some kind of pause to release the bus */
    }
    save = item;
    item = DAGUE_LIFO_PTR(item);
    if( item == lifo->lifo_ghost ) return NULL;
    item->keeper_of_the_seven_keys = DAGUE_LIFO_CNT(save);

    DAGUE_DETACH_ELEM(item);
    return item;
}

static inline void dague_atomic_lifo_construct( dague_atomic_lifo_t* lifo )
{
    DAGUE_LIFO_ELT_ALLOC( lifo->lifo_ghost, sizeof(dague_list_item_t));
    lifo->lifo_ghost->list_next = lifo->lifo_ghost;
    lifo->lifo_ghost->list_prev = lifo->lifo_ghost;
    lifo->lifo_head = lifo->lifo_ghost;
}

static inline void dague_atomic_lifo_destruct( dague_atomic_lifo_t *lifo )
{
    DAGUE_LIFO_ELT_FREE( lifo->lifo_ghost );
}

#endif

#endif  /* LIFO_H_HAS_BEEN_INCLUDED */
