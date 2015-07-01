/*
 * Peripheral register emulation.
 *
 * Copyright (c) 2015 Liviu Ionescu.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/misc/peripheral-register.h"
#include "hw/misc/peripheral.h"
#include "hw/arm/cortexm-helper.h"

/**
 * This file implements a peripheral register. It extends all shorter accesses
 * to register size and uses the defined masks to write/read the register.
 *
 * If custom read/write actions are needed, it is possible to define new
 * types that redefine these methods.
 */

static void derived_peripheral_register_instance_init_callback(Object *obj);
static void derived_peripheral_register_class_init_callback(ObjectClass *klass,
        void *data);

/* ----- Public ------------------------------------------------------------ */

uint64_t peripheral_register_get_value(Object* obj)
{
    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(obj);

    return (state->value & state->readable_bits);
}

/**
 * Create a dynamic instance of the peripheral register object, using the
 * definitions in the Info structure. If present, children bitfields are
 * also created.
 */
Object *peripheral_register_new(Object *parent, const char *node_name,
        PeripheralRegisterInfo *info)
{
    Object *reg = cm_object_new(parent, node_name,
    TYPE_PERIPHERAL_REGISTER);

    cm_object_property_set_str(reg, node_name, "name");

    cm_object_property_set_int(reg, info->offset_bytes, "offset-bytes");
    if (info->reset_value != 0) {
        cm_object_property_set_int(reg, info->reset_value, "reset-value");
    }
    if (info->readable_bits != 0) {
        cm_object_property_set_int(reg, info->readable_bits, "readable-bits");
    }
    if (info->access_flags != 0) {
        cm_object_property_set_int(reg, info->access_flags, "access-flags");
    }

    int size_bits = 0;
    if (info->size_bits != 0) {
        size_bits = info->size_bits;
    } else {
        size_bits = PERIPHERAL_STATE(parent)->register_size_bytes * 8;
    }
    cm_object_property_set_int(reg, size_bits, "size-bits");

    if (info->rw_mode != 0) {
        if (info->rw_mode & REGISTER_RW_MODE_READ) {
            cm_object_property_set_bool(reg, true, "is-readable");
        } else {
            cm_object_property_set_bool(reg, false, "is-readable");
        }
        if (info->rw_mode & REGISTER_RW_MODE_WRITE) {
            cm_object_property_set_bool(reg, true, "is-writable");
        } else {
            cm_object_property_set_bool(reg, false, "is-writable");
        }
    } else {
        cm_object_property_set_bool(reg, true, "is-readable");
        cm_object_property_set_bool(reg, true, "is-writable");
    }

    if (info->bitfields) {

        RegisterBitfieldInfo *bifi_info;
        for (bifi_info = info->bitfields; bifi_info->name; ++bifi_info) {

            Object *bifi = cm_object_new(reg, bifi_info->name,
            TYPE_REGISTER_BITFIELD);

            cm_object_property_set_str(bifi, bifi_info->name, "name");

            assert(bifi_info->first_bit < 32);
            cm_object_property_set_int(bifi, bifi_info->first_bit, "first-bit");

            assert(bifi_info->last_bit < 32);
            if (bifi_info->last_bit != 0) {
                cm_object_property_set_int(bifi, bifi_info->last_bit,
                        "last-bit");
            }

            if (bifi_info->reset_value != 0) {
                cm_object_property_set_int(bifi, bifi_info->reset_value,
                        "reset-value");
            }

            if (bifi_info->rw_mode != 0) {
                if (bifi_info->rw_mode & REGISTER_RW_MODE_READ) {
                    cm_object_property_set_bool(bifi, true, "is-readable");
                } else {
                    cm_object_property_set_bool(bifi, false, "is-readable");
                }
                if (bifi_info->rw_mode & REGISTER_RW_MODE_WRITE) {
                    cm_object_property_set_bool(bifi, true, "is-writable");
                } else {
                    cm_object_property_set_bool(bifi, false, "is-writable");
                }
            } else {
                /*
                 * Leave both false, as set by the option defaults,
                 * in bitfield realize() this dual condition is tested to
                 * compute the actual values using parent values.
                 */
            }

            cm_object_property_set_int(bifi, size_bits, "register-size-bits");

            if (bifi_info->follows != NULL && strlen(bifi_info->follows) > 0) {
                cm_object_property_set_str(bifi, bifi_info->follows, "follows");
            }

            if (bifi_info->cleared_by != NULL
                    && strlen(bifi_info->cleared_by) > 0) {
                cm_object_property_set_str(bifi, bifi_info->cleared_by,
                        "cleared-by");
            }

            if (bifi_info->set_by != NULL && strlen(bifi_info->set_by) > 0) {
                cm_object_property_set_str(bifi, bifi_info->set_by, "set-by");
            }

            /* Should we delay until the register is realized()? */
            cm_object_realize(bifi);
        }
    }

    return reg;
}

Object *derived_peripheral_register_new(Object *parent, const char *node_name,
        const char *type_name)
{
    Object *obj = cm_object_new(parent, node_name, type_name);
    return obj;
}

void derived_peripheral_register_type_register(PeripheralRegisterTypeInfo *reg,
        const char *type_name)
{
    TypeInfo ti = {
        .name = type_name,
        .parent = TYPE_PERIPHERAL_REGISTER,
        .instance_init = derived_peripheral_register_instance_init_callback,
        .class_init = derived_peripheral_register_class_init_callback,
        .class_data = (void *) reg, /**/
    };

    type_register(&ti);
}

/* ----- Private ----------------------------------------------------------- */

/**
 * Structure used to process endianness.
 * It overlaps a long long with an array of bytes.
 */
typedef union {
    uint64_t ll;
    uint8_t b[8];
} EndiannessUnion;

static uint64_t peripheral_register_read_callback(Object *reg, Object *periph,
        uint32_t addr, uint32_t offset, unsigned size)
{
    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(reg);
    PeripheralState *periph_state = PERIPHERAL_STATE(periph);

    EndiannessUnion tmp;
    tmp.ll = state->value & state->readable_bits;

    EndiannessUnion result;
    result.ll = 0; /* Start with a zero value. */

    /*
     * Copy 'size' bytes from register to response.
     * The other bytes remain zero.
     * Working at byte level, it copes well with any alignment.
     */
    int i;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (periph_state->is_little_endian) {
        for (i = 0; i < size; ++i) {
            /*
             * Source: little-endian (guest register)
             * Destination: little-endian (host result)
             */
            result.b[i] = tmp.b[i + offset];
        }
    } else {
        /*
         * Source: big-endian (guest register)
         * Destination: little-endian (host result)
         */
        for (i = 0; i < size; ++i) {
            result.b[i] = tmp.b[8 - (i + offset)];
        }
    }
#else
    /* Warning: Not tested! */
    if (periph_state->is_little_endian) {
        for (i = 0; i < size; ++i) {
            /*
             * Source: little-endian (guest register)
             * Destination: big-endian (host result)
             */
            result.b[8 - i] = tmp.b[i + offset];
        }
    } else {
        for (i = 0; i < size; ++i) {
            /*
             * Source: big-endian (guest register)
             * Destination: big-endian (host result)
             */
            result.b[8 - i] = tmp.b[8 - (i + offset)];
        }
    }
#endif
    return result.ll;
}

static void peripheral_register_write_callback(Object *reg, Object *periph,
        uint32_t addr, uint32_t offset, unsigned size, uint64_t value)
{
    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(reg);
    PeripheralState *periph_state = PERIPHERAL_STATE(periph);

    EndiannessUnion in_value;
    in_value.ll = value;

    EndiannessUnion new_value;
    new_value.ll = state->value; /* Start with the original value. */

    /*
     * Overwrite 'size' bytes in new_value with bytes from in_value.
     * The other bytes remain with their original values.
     * Working at byte level, it copes well with any alignment.
     */
    int i;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (periph_state->is_little_endian) {
        for (i = 0; i < size; ++i) {
            /*
             * Source: little-endian (host in value)
             * Destination: little-endian (guest register)
             */
            new_value.b[i + offset] = in_value.b[i];
        }
    } else {
        for (i = 0; i < size; ++i) {
            /*
             * Source: little-endian (host in value)
             * Destination: big-endian (guest register)
             */
            new_value.b[8 - (i + offset)] = in_value.b[i];
        }
    }
#else
    /* Warning: Not tested! */
    if (periph_state->is_little_endian) {
        for (i = 0; i < size; ++i) {
            /*
             * Source: big-endian (host in value)
             * Destination: little-endian (guest register)
             */
            new_value.b[i + offset] = in_value.b[8 - i];
        }
    } else {
        for (i = 0; i < size; ++i) {
            /*
             * Source: big-endian (host in value)
             * Destination: big-endian (guest register)
             */
            new_value.b[8 - (i + offset)] = in_value.b[8 - i];
        }
    }
#endif

    uint64_t tmp;
    tmp = state->value & (~state->writable_bits);
    tmp |= (new_value.ll & state->writable_bits);

    PeripheralRegisterAutoBits *auto_bits;
    for (auto_bits = state->auto_bits; auto_bits && auto_bits->mask;
            auto_bits++) {
        if (auto_bits->type == PERIPHERAL_REGISTER_AUTO_BITS_TYPE_FOLLOWS) {
            /* Clear the linked bits and copy those from the referred bits. */
            if (auto_bits->shift > 0) {
                tmp &= ~(auto_bits->mask << auto_bits->shift);
                tmp |= ((tmp & auto_bits->mask) << auto_bits->shift);
            } else if (auto_bits->shift < 0) {
                tmp &= ~(auto_bits->mask >> auto_bits->shift);
                tmp |= ((tmp & auto_bits->mask) >> auto_bits->shift);
            }
        } else if (auto_bits->type
                == PERIPHERAL_REGISTER_AUTO_BITS_TYPE_CLEARED_BY) {
            /* If the referred bits are set, clear the linked bits. */
            if (auto_bits->shift > 0) {
                tmp &= ~((tmp & auto_bits->mask) << auto_bits->shift);
            } else if (auto_bits->shift < 0) {
                tmp &= ~((tmp & auto_bits->mask) >> auto_bits->shift);
            }
        } else if (auto_bits->type
                == PERIPHERAL_REGISTER_AUTO_BITS_TYPE_SET_BY) {
            /* If the referred bits are set, set the linked bits. */
            if (auto_bits->shift > 0) {
                tmp |= ((tmp & auto_bits->mask) << auto_bits->shift);
            } else if (auto_bits->shift < 0) {
                tmp |= ((tmp & auto_bits->mask) >> auto_bits->shift);
            }
        }
    }

    state->value = tmp;
}

/* ------------------------------------------------------------------------- */

static void peripheral_register_instance_init_callback(Object *obj)
{
    qemu_log_function_name();

    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(obj);

    /*
     * Add properties and set the default values.
     */
    cm_object_property_add_const_str(obj, "name", &state->name);
    state->name = NULL;

    cm_object_property_add_uint32(obj, "offset-bytes", &state->offset_bytes);
    state->offset_bytes = 0x00000000;

    cm_object_property_add_uint64(obj, "reset-value", &state->reset_value);
    state->reset_value = 0x0000000000000000;

    cm_object_property_add_uint64(obj, "readable-bits", &state->readable_bits);
    state->readable_bits = 0x0000000000000000;

    cm_object_property_add_uint64(obj, "writable-bits", &state->writable_bits);
    state->writable_bits = 0x0000000000000000;

    cm_object_property_add_uint32(obj, "access-flags", &state->access_flags);
    state->access_flags = PERIPHERAL_REGISTER_DEFAULT_ACCESS_FLAGS;

    cm_object_property_add_uint32(obj, "size-bits", &state->size_bits);
    /* Intentional 0. Default set again in realize(), possibly from parent. */
    state->size_bits = 0;

    cm_object_property_add_bool(obj, "is-readable", &state->is_readable);
    state->is_readable = true;

    cm_object_property_add_bool(obj, "is-writable", &state->is_writable);
    state->is_writable = true;

    /* Reset value. */
    state->value = 0x00000000;

    state->auto_bits = NULL;
}

typedef struct {
    uint64_t mask;
    uint64_t readable_bits;
    uint64_t writable_bits;
    uint64_t reset_value;
    PeripheralRegisterState *reg;
    Error *local_err;
} PeripheralRegisterValidateTmp;

/**
 * Create the auto_bits array, by concatenating masks of
 * followed bitfields and grouping based on shift steps.
 */
static int peripheral_register_validate_bitfields(Object *obj, void *opaque)
{
    PeripheralRegisterValidateTmp *validate_tmp =
            (PeripheralRegisterValidateTmp *) opaque;
    PeripheralRegisterState *reg = validate_tmp->reg;

    /* Process only children that descend from a bitfield. */
    if (cm_object_is_instance_of_typename(obj, TYPE_REGISTER_BITFIELD)) {
        RegisterBitfieldState *bifi = REGISTER_BITFIELD_STATE(obj);

        if (bifi->mask & validate_tmp->mask) {
            error_setg(&validate_tmp->local_err, "Bitfield %s of register %s  "
                    "overlaps with other bitfield.\n", bifi->name, reg->name);
            return 1;
        }

        /* Collect more bits in the mask. */
        validate_tmp->mask |= bifi->mask;

        /* Collect readable bits. */
        if (bifi->is_readable) {
            validate_tmp->readable_bits |= bifi->mask;
        }
        /* Collect writable bits. */
        if (bifi->is_writable) {
            validate_tmp->writable_bits |= bifi->mask;
        }

        validate_tmp->reset_value &= (~bifi->mask);
        validate_tmp->reset_value |= ((bifi->reset_value << bifi->shift)
                & bifi->mask);
    }
    return 0; /* Continue iterations. */
}

/**
 * Internal structure with temporary storage,
 * used to compute the auto_bits array.
 */
typedef struct {
    uint64_t left_shift_follows_masks[64];
    uint64_t right_shift_follows_masks[64];

    uint64_t left_shift_cleared_by_masks[64];
    uint64_t right_shift_cleared_by_masks[64];

    uint64_t left_shift_set_by_masks[64];
    uint64_t right_shift_set_by_masks[64];

    const char *to_find_bifi;
    RegisterBitfieldState *found_bifi;

    PeripheralRegisterState *reg;
    Error *local_err;
} PeripheralRegisterAutoTmp;

/**
 * Find the followed bitfield among its siblings.
 * Use the temporary structure to pass input/output values.
 * When found, break the iteration.
 */
static int peripheral_register_find_bifi(Object *obj, void *opaque)
{
    PeripheralRegisterAutoTmp *auto_tmp = (PeripheralRegisterAutoTmp *) opaque;

    /* Process only children that descend from a bitfield. */
    if (cm_object_is_instance_of_typename(obj, TYPE_REGISTER_BITFIELD)) {
        RegisterBitfieldState *bifi = REGISTER_BITFIELD_STATE(obj);

        if (strcmp(auto_tmp->to_find_bifi, bifi->name) == 0) {
            auto_tmp->found_bifi = bifi;
            return 1; /* Break iterations. */
        }
    }

    return 0; /* Continue iterations. */
}

/**
 * Create the auto_bits array, by concatenating masks of
 * followed bitfields and grouping based on shift steps.
 */
static int peripheral_register_create_auto_array(Object *obj, void *opaque)
{
    PeripheralRegisterAutoTmp *auto_tmp = (PeripheralRegisterAutoTmp *) opaque;
    PeripheralRegisterState *reg = auto_tmp->reg;

    /* Process only children that descend from a bitfield. */
    if (cm_object_is_instance_of_typename(obj, TYPE_REGISTER_BITFIELD)) {
        RegisterBitfieldState *bifi = REGISTER_BITFIELD_STATE(obj);

        if (bifi->follows) {
            /* Find the followed bitfield. */
            auto_tmp->to_find_bifi = bifi->follows;
            auto_tmp->found_bifi = NULL;

            /* Try to find the followed bitfield among its siblings. */
            object_child_foreach(OBJECT(reg), peripheral_register_find_bifi,
                    (void *) auto_tmp);

            if (auto_tmp->found_bifi == NULL) {
                error_setg(&auto_tmp->local_err,
                        "Bitfield %s of register %s follows "
                                "missing %s bitfield.\n", bifi->name, reg->name,
                        auto_tmp->to_find_bifi);
                return 1;
            }

            /* Positive values means shift left, negative shift right. */
            int delta_shift = bifi->first_bit - auto_tmp->found_bifi->first_bit;
            if (delta_shift > 0) {
                auto_tmp->left_shift_follows_masks[delta_shift] |=
                        auto_tmp->found_bifi->mask;
            } else if (delta_shift < 0) {
                auto_tmp->right_shift_follows_masks[-delta_shift] |=
                        auto_tmp->found_bifi->mask;
            }
        } else if (bifi->cleared_by) {
            /* Find the .cleared_by bitfield. */
            auto_tmp->to_find_bifi = bifi->cleared_by;
            auto_tmp->found_bifi = NULL;

            /* Try to find the followed bitfield among its siblings. */
            object_child_foreach(OBJECT(reg), peripheral_register_find_bifi,
                    (void *) auto_tmp);

            if (auto_tmp->found_bifi == NULL) {
                error_setg(&auto_tmp->local_err,
                        "Bitfield %s of register %s cleared by "
                                "missing %s bitfield.\n", bifi->name, reg->name,
                        auto_tmp->to_find_bifi);
                return 1;
            }

            /* Positive values means shift left, negative shift right. */
            int delta_shift = bifi->first_bit - auto_tmp->found_bifi->first_bit;
            if (delta_shift > 0) {
                auto_tmp->left_shift_cleared_by_masks[delta_shift] |=
                        auto_tmp->found_bifi->mask;
            } else if (delta_shift < 0) {
                auto_tmp->right_shift_cleared_by_masks[-delta_shift] |=
                        auto_tmp->found_bifi->mask;
            }
        } else if (bifi->set_by) {
            /* Find the .set_by bitfield. */
            auto_tmp->to_find_bifi = bifi->cleared_by;
            auto_tmp->found_bifi = NULL;

            /* Try to find the followed bitfield among its siblings. */
            object_child_foreach(OBJECT(reg), peripheral_register_find_bifi,
                    (void *) auto_tmp);

            if (auto_tmp->found_bifi == NULL) {
                error_setg(&auto_tmp->local_err,
                        "Bitfield %s of register %s set by "
                                "missing %s bitfield.\n", bifi->name, reg->name,
                        auto_tmp->to_find_bifi);
                return 1;
            }

            /* Positive values means shift left, negative shift right. */
            int delta_shift = bifi->first_bit - auto_tmp->found_bifi->first_bit;
            if (delta_shift > 0) {
                auto_tmp->left_shift_set_by_masks[delta_shift] |=
                        auto_tmp->found_bifi->mask;
            } else if (delta_shift < 0) {
                auto_tmp->right_shift_set_by_masks[-delta_shift] |=
                        auto_tmp->found_bifi->mask;
            }
        }
    }

    return 0; /* Continue iterations. */
}

static void peripheral_register_realize_callback(DeviceState *dev, Error **errp)
{
    qemu_log_function_name();

    /* Call parent realize(). */
    if (!cm_device_parent_realize(dev, errp, TYPE_PERIPHERAL_REGISTER)) {
        return;
    }

    /*
     * By the time we reach here, the bitfields were already realized().
     * This also means the readable/writable masks might have been
     * updated.
     */

    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(dev);

    if (state->size_bits == 0) {

        PeripheralState *parent = PERIPHERAL_STATE(
                cm_object_get_parent(OBJECT(dev)));

        if (parent->register_size_bytes != 0) {
            state->size_bits = parent->register_size_bytes * 8;
        } else {
            state->size_bits = PERIPHERAL_REGISTER_DEFAULT_SIZE_BYTES;
        }
    }

    int ret;
    /*
     * Scan bitfields and validate by checking if masks do not overlap.
     */
    PeripheralRegisterValidateTmp *validate_tmp = g_malloc0(
            sizeof(PeripheralRegisterValidateTmp));
    validate_tmp->reg = state;
    validate_tmp->mask = 0;

    ret = object_child_foreach(OBJECT(dev),
            peripheral_register_validate_bitfields, (void *) validate_tmp);

    if (ret) {
        if (validate_tmp->local_err) {
            error_propagate(errp, validate_tmp->local_err);
        }
    }
    int has_bitfields = (validate_tmp->mask != 0);
    uint64_t bitfields_mask = validate_tmp->mask;
    uint64_t bitfields_readable_bits = validate_tmp->readable_bits;
    uint64_t bitfields_writable_bits = validate_tmp->writable_bits;
    uint64_t bitfields_reset_value = validate_tmp->reset_value;

    g_free(validate_tmp);

    if (ret) {
        return;
    }

    if (has_bitfields) {
        /*
         * If it has bitfields, to the defined readable/writable masks
         * the bitfields will contribute more bits.
         */
        state->readable_bits |= bitfields_readable_bits;
        state->writable_bits |= bitfields_writable_bits;
    } else {
        /*
         * It has no bitfields, we might need to do our best to
         * determine if there are some bitmasks, otherwise set
         * them to allow all bits.
         */
        if (state->readable_bits == 0 && state->is_readable) {
            /* Default all bits readable. */
            state->readable_bits = 0xFFFFFFFFFFFFFFFF;
        }

        if (state->writable_bits == 0 && state->is_writable) {
            /* Default all bits writable. */
            state->writable_bits = 0xFFFFFFFFFFFFFFFF;
        }
    }

    /* Clear readable bits if entire register is non-readable. */
    if (!state->is_readable) {
        state->readable_bits = 0;
    }

    /* Clear writable bits if entire register is non-writable. */
    if (!state->is_writable) {
        state->writable_bits = 0;
    }

    state->reset_value &= ~(bitfields_reset_value & bitfields_mask);
    state->reset_value |= (bitfields_reset_value & bitfields_mask);

    /*
     * Scan children bitfields to identify those that follow other
     * bitfields. Compute the signed distance between bitfields
     * and for each distance accumulate a bitmask.
     */
    PeripheralRegisterAutoTmp *auto_tmp = g_malloc0(
            sizeof(PeripheralRegisterAutoTmp));
    auto_tmp->reg = state;

    state->auto_bits = NULL;

    ret = object_child_foreach(OBJECT(dev),
            peripheral_register_create_auto_array, (void *) auto_tmp);

    if (ret) {
        if (auto_tmp->local_err) {
            error_propagate(errp, auto_tmp->local_err);
        }
    } else {
        int count = 0;
        int i;
        for (i = 0; i < 64; ++i) {
            if (auto_tmp->left_shift_follows_masks[i] != 0) {
                count++;
            }
            if (auto_tmp->right_shift_follows_masks[i] != 0) {
                count++;
            }
            if (auto_tmp->left_shift_cleared_by_masks[i] != 0) {
                count++;
            }
            if (auto_tmp->right_shift_cleared_by_masks[i] != 0) {
                count++;
            }
            if (auto_tmp->left_shift_set_by_masks[i] != 0) {
                count++;
            }
            if (auto_tmp->right_shift_set_by_masks[i] != 0) {
                count++;
            }
        }

        if (count) {
            count++; /* One more for the terminator. */
            PeripheralRegisterAutoBits *auto_bits = g_malloc_n(count,
                    sizeof(PeripheralRegisterAutoBits));

            PeripheralRegisterAutoBits *p = auto_bits;

            for (i = 0; i < 64; ++i) {
                if (auto_tmp->left_shift_follows_masks[i] != 0) {
                    p->mask = auto_tmp->left_shift_follows_masks[i];
                    p->shift = i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_FOLLOWS;
                    ++p;
                }
                if (auto_tmp->right_shift_follows_masks[i] != 0) {
                    p->mask = auto_tmp->right_shift_follows_masks[i];
                    p->shift = -i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_FOLLOWS;
                    ++p;
                }
                if (auto_tmp->left_shift_cleared_by_masks[i] != 0) {
                    p->mask = auto_tmp->left_shift_cleared_by_masks[i];
                    p->shift = i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_CLEARED_BY;
                    ++p;
                }
                if (auto_tmp->right_shift_cleared_by_masks[i] != 0) {
                    p->mask = auto_tmp->right_shift_cleared_by_masks[i];
                    p->shift = -i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_CLEARED_BY;
                    ++p;
                }
                if (auto_tmp->left_shift_set_by_masks[i] != 0) {
                    p->mask = auto_tmp->left_shift_set_by_masks[i];
                    p->shift = i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_SET_BY;
                    ++p;
                }
                if (auto_tmp->right_shift_set_by_masks[i] != 0) {
                    p->mask = auto_tmp->right_shift_set_by_masks[i];
                    p->shift = -i;
                    p->type = PERIPHERAL_REGISTER_AUTO_BITS_TYPE_SET_BY;
                    ++p;
                }
            }

            /* End of array. */
            p->mask = 0;
            p->shift = 0;

            state->auto_bits = auto_bits;
        }
    }
    g_free(auto_tmp);

    if (ret) {
        return;
    }

    qemu_log_mask(LOG_TRACE,
            "%s() '%s', readable: 0x%08llX, writable: 0x%08llX, reset: 0x%08llX, mode: %s%s\n",
            __FUNCTION__, state->name, state->readable_bits,
            state->writable_bits, state->reset_value,
            state->is_readable ? "r" : "", state->is_writable ? "w" : "");
}

static void peripheral_register_reset_callback(DeviceState *dev)
{
    qemu_log_function_name();

    /* Call parent reset(). */
    cm_device_parent_reset(dev, TYPE_PERIPHERAL_REGISTER);

    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(dev);

    /* Initialise the register value with the reset value. */
    state->value = state->reset_value;
}

static void peripheral_register_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = peripheral_register_reset_callback;
    dc->realize = peripheral_register_realize_callback;

    PeripheralRegisterClass *pr_class = PERIPHERAL_REGISTER_CLASS(klass);
    pr_class->read = peripheral_register_read_callback;
    pr_class->write = peripheral_register_write_callback;
}

static const TypeInfo peripheral_register_type_info = {
    .name = TYPE_PERIPHERAL_REGISTER,
    .parent = TYPE_PERIPHERAL_REGISTER_PARENT,
    .instance_init = peripheral_register_instance_init_callback,
    .instance_size = sizeof(PeripheralRegisterState),
    .class_init = peripheral_register_class_init,
    .class_size = sizeof(PeripheralRegisterClass) };

static void register_peripheral_register_types(void)
{
    type_register_static(&peripheral_register_type_info);
}

type_init(register_peripheral_register_types);

/* ------------------------------------------------------------------------- */

static void derived_peripheral_register_instance_init_callback(Object *obj)
{
    qemu_log_function_name();

    const char *type_name = object_get_typename(obj);
    PeripheralRegisterState *state = PERIPHERAL_REGISTER_STATE(obj);
    PeripheralRegisterDerivedClass *klass =
            PERIPHERAL_REGISTER_DERIVED_GET_CLASS(obj, type_name);

    /*
     * After properties are set with default values, copy actual
     * values from class, if present.
     */
    state->offset_bytes = klass->offset_bytes;
    state->reset_value = klass->reset_value;
    if (klass->readable_bits != 0) {
        state->readable_bits = klass->readable_bits;
    }
    if (state->writable_bits != 0) {
        state->writable_bits = klass->writable_bits;
    }
    if (klass->access_flags != 0) {
        state->access_flags = klass->access_flags;
    }
    if (klass->rw_mode != 0) {
        if (klass->rw_mode & REGISTER_RW_MODE_READ) {
            state->is_readable = true;
        } else {
            state->is_readable = false;
        }
        if (klass->rw_mode & REGISTER_RW_MODE_WRITE) {
            state->is_writable = true;
        } else {
            state->is_writable = false;
        }
    } else {
        /* Default both read and write. */
        state->is_readable = true;
        state->is_writable = true;
    }

    if (klass->size_bits != 0) {
        state->size_bits = klass->size_bits;
    }

    state->value = klass->reset_value;
}

static void derived_peripheral_register_class_init_callback(ObjectClass *klass,
        void *data)
{
    PeripheralRegisterTypeInfo *ti = (PeripheralRegisterTypeInfo *) data;
    PeripheralRegisterClass *pr_class = PERIPHERAL_REGISTER_CLASS(klass);

    const char *type_name = object_class_get_name(klass);
    PeripheralRegisterDerivedClass *prd_class =
            PERIPHERAL_REGISTER_DERIVED_CLASS(klass, type_name);

    /*
     * Derived classes can use parent_read() & parent_write() to
     * perform the peripheral read/write's.
     */
    if (ti->read) {
        prd_class->parent_read = pr_class->read;
        pr_class->read = ti->read;
    }
    if (ti->write) {
        prd_class->parent_write = pr_class->write;
        pr_class->write = ti->write;
    }

    /* Copy info members into class. */
    prd_class->name = ti->type_name;
    prd_class->desc = ti->desc;
    prd_class->offset_bytes = ti->offset_bytes;
    prd_class->reset_value = ti->reset_value;
    prd_class->readable_bits = ti->readable_bits;
    prd_class->writable_bits = ti->writable_bits;
    prd_class->access_flags = ti->access_flags;
    prd_class->rw_mode = ti->rw_mode;
    prd_class->size_bits = ti->size_bits;

    prd_class->bitfields = ti->bitfields;
}

/* ------------------------------------------------------------------------- */

