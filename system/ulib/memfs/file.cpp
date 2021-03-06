// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/vfs.h>
#include <fs/vfs.h>
#include <lib/memfs/cpp/vnode.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

namespace memfs {

// Artificially cap the maximum in-memory file size to 512MB.
constexpr size_t kMemfsMaxFileSize = 512 * 1024 * 1024;

VnodeFile::VnodeFile(Vfs* vfs)
    : VnodeMemfs(vfs), length_(0) {}

VnodeFile::VnodeFile(Vfs* vfs, zx_handle_t vmo, zx_off_t length)
    : VnodeMemfs(vfs), vmo_(vmo), length_(length) {}

VnodeFile::~VnodeFile() = default;

zx_status_t VnodeFile::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t VnodeFile::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    if ((off >= length_) || (!vmo_.is_valid())) {
        *out_actual = 0;
        return ZX_OK;
    } else if (len > length_ - off) {
        len = length_ - off;
    }

    zx_status_t status = vmo_.read(data, off, len);
    if (status == ZX_OK) {
        *out_actual = len;
    }
    return status;
}

zx_status_t VnodeFile::Write(const void* data, size_t len, size_t offset,
                             size_t* out_actual) {
    zx_status_t status;
    size_t newlen = offset + len;
    newlen = newlen > kMemfsMaxFileSize ? kMemfsMaxFileSize : newlen;
    size_t alignedlen = fbl::round_up(newlen, static_cast<size_t>(PAGE_SIZE));

    if (!vmo_.is_valid()) {
        // First access to the file? Allocate it.
        if ((status = zx::vmo::create(alignedlen, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    } else if (alignedlen > fbl::round_up(length_, static_cast<size_t>(PAGE_SIZE))) {
        // Accessing beyond the end of the file? Extend it.
        if ((status = vmo_.set_size(alignedlen)) != ZX_OK) {
            return status;
        }
    }

    size_t writelen = newlen - offset;
    if ((status = vmo_.write(data, offset, writelen)) != ZX_OK) {
        return status;
    }
    *out_actual = writelen;

    if (newlen > length_) {
        length_ = newlen;
    }
    if (writelen < len) {
        // short write because we're beyond the end of the permissible length
        return ZX_ERR_FILE_BIG;
    }
    UpdateModified();
    return ZX_OK;
}

zx_status_t VnodeFile::Append(const void* data, size_t len, size_t* out_end,
                              size_t* out_actual) {
    zx_status_t status = Write(data, len, length_, out_actual);
    *out_end = length_;
    return status;
}

zx_status_t VnodeFile::GetVmo(int flags, zx_handle_t* out) {
    zx_status_t status;
    if (!vmo_.is_valid()) {
        // First access to the file? Allocate it.
        if ((status = zx::vmo::create(0, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    }

    // Let clients map and set the names of their VMOs.
    zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
    rights |= (flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
    rights |= (flags & FDIO_MMAP_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
    rights |= (flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
    zx::vmo out_vmo;
    if (flags & FDIO_MMAP_FLAG_PRIVATE) {
        if ((status = vmo_.clone(ZX_VMO_CLONE_COPY_ON_WRITE, 0, length_,
                                 &out_vmo)) != ZX_OK) {
            return status;
        }

        if ((status = out_vmo.replace(rights, &out_vmo)) != ZX_OK) {
            return status;
        }
        *out = out_vmo.release();
        return ZX_OK;
    }

    if ((status = vmo_.duplicate(rights, &out_vmo)) != ZX_OK) {
        return status;
    }
    *out = out_vmo.release();
    return ZX_OK;
}

zx_status_t VnodeFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->inode = ino_;
    attr->mode = V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH;
    attr->size = length_;
    attr->blksize = kMemfsBlksize;
    attr->blkcount = fbl::round_up(attr->size, kMemfsBlksize) / VNATTR_BLKSIZE;
    attr->nlink = link_count_;
    attr->create_time = create_time_;
    attr->modify_time = modify_time_;
    return ZX_OK;
}

zx_status_t VnodeFile::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                  zxrio_object_info_t* extra) {
    *type = FDIO_PROTOCOL_FILE;
    return ZX_OK;
}

zx_status_t VnodeFile::Truncate(size_t len) {
    zx_status_t status;
    if (len > kMemfsMaxFileSize) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t alignedLen = fbl::round_up(len, static_cast<size_t>(PAGE_SIZE));

    if (!vmo_.is_valid()) {
        // First access to the file? Allocate it.
        if ((status = zx::vmo::create(alignedLen, 0, &vmo_)) != ZX_OK) {
            return status;
        }
    } else if ((len < length_) && (len % PAGE_SIZE != 0)) {
        // Currently, if the file is truncated to a 'partial page', an later re-expanded, then the
        // partial page is *not necessarily* filled with zeroes. As a consequence, we manually must
        // fill the portion between "len" and the next highest page (or vn->length, whichever
        // is smaller) with zeroes.
        char buf[PAGE_SIZE];
        size_t ppage_size = PAGE_SIZE - (len % PAGE_SIZE);
        ppage_size = len + ppage_size < length_ ? ppage_size : length_ - len;
        memset(buf, 0, ppage_size);
        if (vmo_.write(buf, len, ppage_size) != ZX_OK) {
            return ZX_ERR_IO;
        }
        if ((status = vmo_.set_size(alignedLen)) != ZX_OK) {
            return status;
        }
    } else if ((status = vmo_.set_size(alignedLen)) != ZX_OK) {
        return status;
    }

    length_ = len;
    UpdateModified();
    return ZX_OK;
}

} // namespace memfs
