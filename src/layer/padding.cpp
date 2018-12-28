// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "padding.h"
#include <math.h>

namespace ncnn {

DEFINE_LAYER_CREATOR(Padding)

Padding::Padding()
{
    one_blob_only = true;
    support_inplace = false;
    support_vulkan = true;
}

int Padding::load_param(const ParamDict& pd)
{
    top = pd.get(0, 0);
    bottom = pd.get(1, 0);
    left = pd.get(2, 0);
    right = pd.get(3, 0);
    type = pd.get(4, 0);
    value = pd.get(5, 0.f);

#if NCNN_VULKAN
    if (pd.use_vulkan_compute)
    {
        local_size_z = std::min(128, vkdev->info.max_workgroup_size[2]);

        int local_size_xy = sqrt(vkdev->info.max_workgroup_invocations / local_size_z);
        int local_size_xy_prefer = 256;
        while (local_size_xy < local_size_xy_prefer)
        {
            local_size_xy_prefer /= 2;
        }
        local_size_x = local_size_xy_prefer;
        local_size_y = local_size_xy_prefer;

        fprintf(stderr, "local size = %d %d %d\n", local_size_x, local_size_y, local_size_z);

        // setup pipeline specializations
        specializations.resize(6);
        specializations[0] = top;
        specializations[1] = bottom;
        specializations[2] = left;
        specializations[3] = right;
        specializations[4] = type;
        specializations[5] = value;

        binding_count = 2;
        push_constant_count = 10;
    }
#endif // NCNN_VULKAN

    return 0;
}

template<typename T>
static void copy_make_border_image(const Mat& src, Mat& dst, int top, int left, int type, T v)
{
    int w = dst.w;
    int h = dst.h;

    const T* ptr = src;
    T* outptr = dst;

    if (type == 0)
    {
        int y = 0;
        // fill top
        for (; y < top; y++)
        {
            int x = 0;
            for (; x < w; x++)
            {
                outptr[x] = v;
            }
            outptr += w;
        }
        // fill center
        for (; y < (top + src.h); y++)
        {
            int x = 0;
            for (; x < left; x++)
            {
                outptr[x] = v;
            }
            if (src.w < 12)
            {
                for (; x < (left + src.w); x++)
                {
                    outptr[x] = ptr[x - left];
                }
            }
            else
            {
                memcpy(outptr + left, ptr, src.w * sizeof(T));
                x += src.w;
            }
            for (; x < w; x++)
            {
                outptr[x] = v;
            }
            ptr += src.w;
            outptr += w;
        }
        // fill bottom
        for (; y < h; y++)
        {
            int x = 0;
            for (; x < w; x++)
            {
                outptr[x] = v;
            }
            outptr += w;
        }
    }
    else if (type == 1)
    {
        int y = 0;
        // fill top
        for (; y < top; y++)
        {
            int x = 0;
            for (; x < left; x++)
            {
                outptr[x] = ptr[0];
            }
            if(src.w < 12)
            {
                for (; x < (left + src.w); x++)
                {
                    outptr[x] = ptr[x - left];
                }
            }
            else
            {
                memcpy(outptr + left, ptr, src.w * sizeof(T));
                x += src.w;
            }
            for (; x < w; x++)
            {
                outptr[x] = ptr[src.w - 1];
            }
            outptr += w;
        }
        // fill center
        for (; y < (top + src.h); y++)
        {
            int x = 0;
            for (; x < left; x++)
            {
                outptr[x] = ptr[0];
            }
            if(src.w < 12)
            {
                for (; x < (left + src.w); x++)
                {
                    outptr[x] = ptr[x - left];
                }
            }
            else
            {
                memcpy(outptr + left, ptr, src.w * sizeof(T));
                x += src.w;
            }
            for (; x < w; x++)
            {
                outptr[x] = ptr[src.w - 1];
            }
            ptr += src.w;
            outptr += w;
        }
        // fill bottom
        ptr -= src.w;
        for (; y < h; y++)
        {
            int x = 0;
            for (; x < left; x++)
            {
                outptr[x] = ptr[0];
            }
            if(src.w < 12)
            {
                for (; x < (left + src.w); x++)
                {
                    outptr[x] = ptr[x - left];
                }
            }
            else
            {
                memcpy(outptr + left, ptr, src.w * sizeof(T));
                x += src.w;
            }
            for (; x < w; x++)
            {
                outptr[x] = ptr[src.w - 1];
            }
            outptr += w;
        }
    }
}

int Padding::forward(const Mat& bottom_blob, Mat& top_blob, const Option& opt) const
{
    if (top == 0 && bottom == 0 && left == 0 && right == 0)
    {
        top_blob = bottom_blob;
        return 0;
    }

    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;

    int outw = w + left + right;

    if (dims == 1)
    {
        top_blob.create(outw, elemsize, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (elemsize == 1)
            copy_make_border_image<signed char>(bottom_blob, top_blob, 0, left, type, value);
        else if (elemsize == 4)
            copy_make_border_image<float>(bottom_blob, top_blob, 0, left, type, value);

        return 0;
    }

    int outh = h + top + bottom;

    if (dims == 2)
    {
        top_blob.create(outw, outh, elemsize, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        if (elemsize == 1)
            copy_make_border_image<signed char>(bottom_blob, top_blob, top, left, type, value);
        else if (elemsize == 4)
            copy_make_border_image<float>(bottom_blob, top_blob, top, left, type, value);

        return 0;
    }

    if (dims == 3)
    {
        top_blob.create(outw, outh, channels, elemsize, opt.blob_allocator);
        if (top_blob.empty())
            return -100;

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            const Mat m = bottom_blob.channel(q);
            Mat borderm = top_blob.channel(q);

            if (elemsize == 1)
                copy_make_border_image<signed char>(m, borderm, top, left, type, value);
            else if (elemsize == 4)
                copy_make_border_image<float>(m, borderm, top, left, type, value);
        }

        return 0;
    }

    return 0;
}

#if NCNN_VULKAN
int Padding::forward(const VkMat& bottom_blob, VkMat& top_blob, Command& cmd, const Option& opt) const
{
    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;

    int outw = w + left + right;
    int outh = h + top + bottom;

    top_blob.create(outw, outh, channels, 4u, opt.blob_vkallocator, opt.staging_vkallocator);
    if (top_blob.empty())
        return -100;

    fprintf(stderr, "Padding::forward %p %p\n", bottom_blob.buffer, top_blob.buffer);

    std::vector<VkMat> bindings(2);
    bindings[0] = bottom_blob;
    bindings[1] = top_blob;

    std::vector<int> constants(10);
    constants[0] = bottom_blob.dims;
    constants[1] = bottom_blob.w;
    constants[2] = bottom_blob.h;
    constants[3] = bottom_blob.c;
    constants[4] = bottom_blob.cstep;
    constants[5] = top_blob.dims;
    constants[6] = top_blob.w;
    constants[7] = top_blob.h;
    constants[8] = top_blob.c;
    constants[9] = top_blob.cstep;

    uint32_t group_count_xyz[3];
    group_count_xyz[0] = (top_blob.w + local_size_x - 1) / local_size_x;
    group_count_xyz[1] = (top_blob.h + local_size_y - 1) / local_size_y;
    group_count_xyz[2] = (top_blob.c + local_size_z - 1) / local_size_z;

    // record
    cmd.record_bind_pipeline(pipeline);
    cmd.record_update_bindings(pipeline_layout, descriptor_update_template, bindings);
    cmd.record_push_constants(pipeline_layout, constants);
    cmd.record_dispatch(group_count_xyz);

    return 0;
}
#endif // NCNN_VULKAN

} // namespace ncnn
