/*
 * Copyright (c) 2015-2024 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#if !defined(PARSEC_CONFIG_H_HAS_BEEN_INCLUDED)
#error data_internal.h header should only be used after parsec_config.h has been included.
#endif  /* !defined(PARSEC_CONFIG_H_HAS_BEEN_INCLUDED) */

#ifndef DATA_INTERNAL_H_HAS_BEEN_INCLUDED
#define DATA_INTERNAL_H_HAS_BEEN_INCLUDED

/** @addtogroup parsec_internal_data
 *  @{
 */

#include "parsec/class/parsec_object.h"
#include "parsec/arena.h"
#include "parsec/data.h"
#include "parsec/class/parsec_future.h"

/**
 * This structure is the keeper of all the information regarding
 * each unique data that can be handled by the system. It contains
 * pointers to the versions managed by each supported devices.
 */
struct parsec_data_s {
    parsec_object_t            super;

    parsec_atomic_lock_t       lock;

    parsec_data_key_t          key;
    int8_t                     owner_device;
    int8_t                     preferred_device; /* Hint set from the MEMADVICE device API to define on
                                                  * which device this data should be modified RW when there
                                                  * are multiple choices. -1 means no preference. */
    struct parsec_data_collection_s*     dc;
    size_t                     nb_elts;          /* size in bytes of the memory layout */
    struct parsec_data_copy_s *device_copies[];  /* this array allocated according to the number of devices
                                                  * (parsec_nb_devices). It points to the most recent
                                                  * version of the data.
                                                  */
};
PARSEC_DECLSPEC PARSEC_OBJ_CLASS_DECLARATION(parsec_data_t);

/**
 * This structure represent a device copy of a parsec_data_t.
 */
struct parsec_data_copy_s {
    parsec_list_item_t          super;

    int8_t                      device_index;         /**< Index in the original->device_copies array */
    parsec_data_flag_t          flags;
    parsec_data_coherency_t     coherency_state;
    /* int8_t */

    int32_t                     readers;

    uint32_t                    version;

    struct parsec_data_copy_s   *older;                 /**< unused yet */
    parsec_data_t               *original;
    struct parsec_arena_chunk_s *arena_chunk;        /**< If this is an arena-based data, keep
                                                      *   the chunk pointer here, to avoid
                                                      *   risky pointers arithmetic (pointers mis-alignment
                                                      *   depending on many parameters) */
    void                     *device_private;        /**< The pointer to the device-specific data.
                                                      *   Overlay data distributions assume that arithmetic
                                                      *   can be done on these pointers. */
    parsec_data_status_t      data_transfer_status;  /**< Have we scheduled a communication to update this data yet?
                                                      *   Possible values are NOT_TRANSFER, UNDER_TRANSFER, TRANSFER_COMPLETE.
                                                      *   NB: this closely follows, but is not equivalent, to
                                                      *   the coherency_flag INVALID. A data copy that is 'under transfer'
                                                      *   is always INVALID. However, a data copy that is INVALID could be
                                                      *   so for many reasons, not necessarily because a transfer is ongoing.
                                                      *   We use this transfer_status to guard scheduling multiple transfers
                                                      *   on the same data. */
    parsec_datatype_t            dtt;                /**< the appropriate type for the network engine to send an element */
    parsec_data_copy_alloc_cb   *alloc_cb;           /**< callback to allocate data copy memory */
    parsec_data_copy_release_cb *release_cb;         /**< callback to release data copy memory */
};

#define PARSEC_DATA_CREATE_ON_DEMAND ((parsec_data_copy_t*)(intptr_t)(-1))

PARSEC_DECLSPEC PARSEC_OBJ_CLASS_DECLARATION(parsec_data_copy_t);

#define PARSEC_DATA_GET_COPY(DATA, DEVID) \
    ((DATA)->device_copies[(DEVID)])
/**
 * Decrease the refcount of this copy of the data. If the refcount reach
 * 0 the upper level is in charge of cleaning up and releasing all content
 * of the copy.
 */
#define PARSEC_DATA_COPY_RELEASE(DATA)     \
    do {                                  \
        PARSEC_DEBUG_VERBOSE(20, parsec_debug_output, "Release data copy %p at %s:%d", (DATA), __FILE__, __LINE__); \
        PARSEC_OBJ_RELEASE((DATA));                                            \
    } while(0)

/**
 * Return the device private pointer for a datacopy.
 */
#define PARSEC_DATA_COPY_GET_PTR(DATA) \
    ((DATA) ? (DATA)->device_private : NULL)

/** @} */

#endif  /* DATA_INTERNAL_H_HAS_BEEN_INCLUDED */
