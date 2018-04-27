// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/auxdata.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct pciroot_protocol_ops {
    zx_status_t (*get_auxdata)(void* ctx, const char* args, void* data,
                               uint32_t bytes, uint32_t* actual);
    zx_status_t (*get_bti)(void* ctx, uint32_t bdf, uint32_t index, zx_handle_t* bti);
    zx_status_t (*get_pci_mcfgs)(void* ctx, zx_pci_init_arg_t** arg, size_t* size);
} pciroot_protocol_ops_t;

typedef struct pciroot_protocol {
    pciroot_protocol_ops_t* ops;
    void* ctx;
} pciroot_protocol_t;

static inline zx_status_t pciroot_get_auxdata(pciroot_protocol_t* pciroot,
                                              const char* args, void* data,
                                              uint32_t bytes, uint32_t* actual) {
    return pciroot->ops->get_auxdata(pciroot->ctx, args, data, bytes, actual);
}

static inline zx_status_t pciroot_get_bti(pciroot_protocol_t* pciroot,
                                          uint32_t bdf, uint32_t index, zx_handle_t* bti) {
    return pciroot->ops->get_bti(pciroot->ctx, bdf, index, bti);
}

static inline zx_status_t pciroot_get_pci_mcfgs(pciroot_protocol_t* pciroot,
                                                zx_pci_init_arg_t** arg, size_t* size) {
    return pciroot->ops->get_pci_mcfgs(pciroot->ctx, arg, size);
}

__END_CDECLS;
