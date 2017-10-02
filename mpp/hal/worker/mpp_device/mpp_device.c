/*
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define MODULE_TAG "mpp_device"

#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "mpp_env.h"
#include "mpp_log.h"

#include "mpp_device.h"
#include "mpp_platform.h"
#include <linux/types.h>

#include "vpu.h"
//https://stackoverflow.com/questions/22496123/what-is-the-meaning-of-this-macro-iormy-macig-0-int
//https://github.com/dalmatele/kernel/blob/release-4.4/drivers/video/rockchip/vpu/mpp_dev_common.c
#define VPU_IOC_MAGIC                       'l'

#define VPU_IOC_SET_CLIENT_TYPE             _IOW(VPU_IOC_MAGIC, 1, unsigned long)
#define VPU_IOC_GET_HW_FUSE_STATUS          _IOW(VPU_IOC_MAGIC, 2, unsigned long)
#define VPU_IOC_SET_REG                     _IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_GET_REG                     _IOW(VPU_IOC_MAGIC, 4, unsigned long)
#define VPU_IOC_PROBE_IOMMU_STATUS          _IOR(VPU_IOC_MAGIC, 5, unsigned long)
#define VPU_TEST                            _IOR(VPU_IOC_MAGIC, 6, u32)
#define VPU_IOC_WRITE(nr, size)             _IOC(_IOC_WRITE, VPU_IOC_MAGIC, (nr), (size))

typedef struct MppReq_t {
    RK_U32 *req;
    RK_U32  size;
} MppReq;

static RK_U32 mpp_device_debug = 0;


static RK_S32 mpp_device_get_client_type(MppDevCtx *ctx, MppCtxType coding, MppCodingType type)
{
    RK_S32 client_type = -1;

    if (coding == MPP_CTX_ENC)
        client_type = VPU_ENC;
    else { /* MPP_CTX_DEC */
        client_type = VPU_DEC;
        if (ctx->pp_enable)
            client_type = VPU_DEC_PP;
    }
    (void)ctx;
    (void)type;
    (void)coding;

    return client_type;
}

RK_S32 mpp_device_init(MppDevCtx *ctx, MppCtxType coding, MppCodingType type)
{
    RK_S32 dev = -1;
    const char *name = NULL;

    ctx->coding = coding;
    ctx->type = type;
    if (ctx->platform)
        name = mpp_get_platform_dev_name(coding, type, ctx->platform);
    else
        name = mpp_get_vcodec_dev_name(coding, type);
    if (name) {
        mpp_log("Device name: %s", name);
        dev = open(name, O_RDWR);
        if (dev > 0) {
            RK_S32 client_type = mpp_device_get_client_type(ctx, coding, type);
            ctx->client_type = client_type;
            mpp_log("mpp_device_init: %x", VPU_TEST);
            RK_S32 ret = ioctl(dev, VPU_IOC_SET_CLIENT_TYPE, client_type);
            if (ret) {
                mpp_err_f("ioctl VPU_IOC_SET_CLIENT_TYPE failed ret %d errno %d\n",
                          ret, errno);
                close(dev);
                dev = -2;
            }
        } else
            mpp_err_f("failed to open device %s, errno %d, error msg: %s\n",
                      name, errno, strerror(errno));
    } else
        mpp_err_f("failed to find device for coding %d type %d\n", coding, type);

    return dev;
}

MPP_RET mpp_device_deinit(RK_S32 dev)
{
    if (dev > 0)
        close(dev);

    return MPP_OK;
}

MPP_RET mpp_device_send_reg(RK_S32 dev, RK_U32 *regs, RK_U32 nregs)
{
    MPP_RET ret;
    MppReq req;

    if (mpp_device_debug) {
        RK_U32 i;

        for (i = 0; i < nregs; i++) {
            mpp_log("set reg[%03d]: %08x\n", i, regs[i]);
        }
    }

    nregs *= sizeof(RK_U32);
    req.req     = regs;
    req.size    = nregs;
    
    ret = (RK_S32)ioctl(dev, VPU_IOC_SET_REG, &req);
    mpp_log("mpp_device_send_reg: %x - %d", VPU_IOC_SET_REG, ret);
    if (ret) {
        mpp_err_f("ioctl VPU_IOC_SET_REG failed ret %d errno %d %s\n",
                  ret, errno, strerror(errno));
        ret = errno;
    }

    return ret;
}

MPP_RET mpp_device_wait_reg(RK_S32 dev, RK_U32 *regs, RK_U32 nregs)
{
    MPP_RET ret;
    MppReq req;

    nregs *= sizeof(RK_U32);
    req.req     = regs;
    req.size    = nregs;

    ret = (RK_S32)ioctl(dev, VPU_IOC_GET_REG, &req);
    mpp_log("mpp_device_wait_reg: %x - %d", VPU_IOC_GET_REG, ret);
    if (ret) {
        mpp_err_f("ioctl VPU_IOC_GET_REG failed ret %d errno %d %s\n",
                  ret, errno, strerror(errno));
        ret = errno;
    }

    if (mpp_device_debug) {
        RK_U32 i;
        nregs >>= 2;

        for (i = 0; i < nregs; i++) {
            mpp_log("get reg[%03d]: %08x\n", i, regs[i]);
        }
    }

    return ret;
}

MPP_RET mpp_device_send_reg_with_id(RK_S32 dev, RK_S32 id, void *param,
                                    RK_S32 size)
{
    MPP_RET ret = MPP_NOK;

    if (param == NULL) {
        mpp_err_f("input param is NULL");
        return ret;
    }

    ret = (RK_S32)ioctl(dev, VPU_IOC_WRITE(id, size), param);
    mpp_log("mpp_device_send_reg_with_id: %x - %d", VPU_IOC_GET_REG, ret);
    mpp_log("mpp_device_send_reg_with_id: %x - %d", VPU_IOC_WRITE(id, size), ret);
    if (ret) {
        mpp_err_f("ioctl VPU_IOC_WRITE failed ret %d errno %d %s\n",
                  ret, errno, strerror(errno));
        ret = errno;
    }

    return ret;
}

RK_S32 mpp_device_control(MppDevCtx *ctx, MppDevCmd cmd, void* param)
{
    switch (cmd) {
    case MPP_DEV_GET_MMU_STATUS : {
        ctx->mmu_status = 1;
        *((RK_U32 *)param) = ctx->mmu_status;
    } break;
    case MPP_DEV_ENABLE_POSTPROCCESS : {
        ctx->pp_enable = 1;
    } break;
    case MPP_DEV_SET_HARD_PLATFORM : {
        ctx->platform = *((RK_U32 *)param);
    } break;
    default : {
    } break;
    }

    return 0;
}

