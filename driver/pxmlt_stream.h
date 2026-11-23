// SPDX-License-Identifier: GPL-2.0-only
/*
 * PTX driver for PLEX PX-MLT series devices
 *
 * Copyright (c) 2018-2021 nns779
 */

#pragma once

#include "ts_sync.h"

#define PXMLT_DEVICE_TS_SYNC_COUNT	4
#define PXMLT_DEVICE_TS_SYNC_SIZE	(188 * PXMLT_DEVICE_TS_SYNC_COUNT)

struct pxmlt_stream_context {
	struct ptx_chrdev *chrdev[PXMLT_CHRDEV_MAX_NUM];
	u8 remain_buf[PXMLT_DEVICE_TS_SYNC_SIZE];
	size_t remain_len;
	bool synchronized;
};

static void pxmlt_device_stream_process(struct pxmlt_stream_context *stream_ctx,
				      u8 **buf, u32 *len)
{
	u8 *p = *buf;
	u32 remain = *len;

	while (likely(remain >= 188)) {
		if (!stream_ctx->synchronized) {
			u32 i;

			for (i = 0; i < PXMLT_DEVICE_TS_SYNC_COUNT; i++) {
				if ((i + 1) * 188 > remain)
					goto done;
				if (!px4_ts_has_tagged_sync(p[i * 188]))
					break;
			}

			if (i < PXMLT_DEVICE_TS_SYNC_COUNT) {
				p++;
				remain--;
				continue;
			}

			stream_ctx->synchronized = true;
		}

		while (likely(remain >= 188 && px4_ts_has_tagged_sync(p[0]))) {
			u8 id = (p[0] & 0x70) >> 4;

			if (likely(id && id <= PXMLT_CHRDEV_MAX_NUM)) {
				p[0] = 0x47;
				ptx_chrdev_put_stream(stream_ctx->chrdev[id - 1],
						      p, 188);
			}

			p += 188;
			remain -= 188;
		}

		if (remain >= 188)
			stream_ctx->synchronized = false;
	}

done:
	*buf = p;
	*len = remain;
}

static int pxmlt_device_stream_handler(void *context, void *buf, u32 len)
{
	struct pxmlt_stream_context *stream_ctx = context;
	u32 ctx_remain_len = stream_ctx->remain_len;
	u8 *p = buf;
	u32 remain = len;

	while (ctx_remain_len && remain) {
		u32 needed = stream_ctx->synchronized ? 188 : PXMLT_DEVICE_TS_SYNC_SIZE;
		u32 copied = min(remain, needed - ctx_remain_len);
		u8 *pending = stream_ctx->remain_buf;

		memcpy(pending + ctx_remain_len, p, copied);
		ctx_remain_len += copied;
		p += copied;
		remain -= copied;

		if (ctx_remain_len < needed)
			break;

		pxmlt_device_stream_process(stream_ctx, &pending,
					    &ctx_remain_len);
		memmove(stream_ctx->remain_buf, pending, ctx_remain_len);
	}
	stream_ctx->remain_len = ctx_remain_len;

	pxmlt_device_stream_process(stream_ctx, &p, &remain);

	if (unlikely(remain)) {
		memcpy(stream_ctx->remain_buf, p, remain);
		stream_ctx->remain_len = remain;
	}

	return 0;
}
