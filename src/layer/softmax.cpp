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

#include "softmax.h"
#include <float.h>
#include <math.h>
#include <algorithm>

namespace ncnn {

DEFINE_LAYER_CREATOR(Softmax)

Softmax::Softmax()
{
    one_blob_only = true;
    support_inplace = true;
    support_vulkan = true;
}

int Softmax::load_param(const ParamDict& pd)
{
    axis = pd.get(0, 0);

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

        // setup pipeline specializations
        specializations.resize(1);
        specializations[0].i = axis;

        binding_count = 3;
        push_constant_count = 5;
    }
#endif // NCNN_VULKAN

    return 0;
}

int Softmax::forward_inplace(Mat& bottom_top_blob, const Option& opt) const
{
    // value = exp( value - global max value )
    // sum all value
    // value = value / sum

    int dims = bottom_top_blob.dims;
    size_t elemsize = bottom_top_blob.elemsize;

    if (dims == 1) // axis == 0
    {
        int w = bottom_top_blob.w;

        float* ptr = bottom_top_blob;

        float max = -FLT_MAX;
        for (int i=0; i<w; i++)
        {
            max = std::max(max, ptr[i]);
        }

        for (int i=0; i<w; i++)
        {
            ptr[i] = exp(ptr[i] - max);
        }

        float sum = 0.f;
        for (int i=0; i<w; i++)
        {
            sum += ptr[i];
        }

        for (int i=0; i<w; i++)
        {
            ptr[i] /= sum;
        }

        return 0;
    }

    if (dims == 2 && axis == 0)
    {
        int w = bottom_top_blob.w;
        int h = bottom_top_blob.h;

        Mat max;
        max.create(w, elemsize, opt.workspace_allocator);
        if (max.empty())
            return -100;
        max.fill(-FLT_MAX);

        for (int i=0; i<h; i++)
        {
            const float* ptr = bottom_top_blob.row(i);
            for (int j=0; j<w; j++)
            {
                max[j] = std::max(max[j], ptr[j]);
            }
        }

        for (int i=0; i<h; i++)
        {
            float* ptr = bottom_top_blob.row(i);
            for (int j=0; j<w; j++)
            {
                ptr[j] = exp(ptr[j] - max[j]);
            }
        }

        Mat sum;
        sum.create(w, elemsize, opt.workspace_allocator);
        if (sum.empty())
            return -100;
        sum.fill(0.f);

        for (int i=0; i<h; i++)
        {
            const float* ptr = bottom_top_blob.row(i);
            for (int j=0; j<w; j++)
            {
                sum[j] += ptr[j];
            }
        }

        for (int i=0; i<h; i++)
        {
            float* ptr = bottom_top_blob.row(i);
            for (int j=0; j<w; j++)
            {
                ptr[j] /= sum[j];
            }
        }

        return 0;
    }

    if (dims == 2 && axis == 1)
    {
        int w = bottom_top_blob.w;
        int h = bottom_top_blob.h;

        Mat max;
        max.create(h, elemsize, opt.workspace_allocator);
        if (max.empty())
            return -100;

        for (int i=0; i<h; i++)
        {
            const float* ptr = bottom_top_blob.row(i);

            float m = -FLT_MAX;
            for (int j=0; j<w; j++)
            {
                m = std::max(m, ptr[j]);
            }

            max[i] = m;
        }

        for (int i=0; i<h; i++)
        {
            float* ptr = bottom_top_blob.row(i);

            float m = max[i];
            for (int j=0; j<w; j++)
            {
                ptr[j] = exp(ptr[j] - m);
            }
        }

        Mat sum;
        sum.create(h, elemsize, opt.workspace_allocator);
        if (sum.empty())
            return -100;

        for (int i=0; i<h; i++)
        {
            const float* ptr = bottom_top_blob.row(i);

            float s = 0.f;
            for (int j=0; j<w; j++)
            {
                s += ptr[j];
            }

            sum[i] = s;
        }

        for (int i=0; i<h; i++)
        {
            float* ptr = bottom_top_blob.row(i);

            float s = sum[i];
            for (int j=0; j<w; j++)
            {
                ptr[j] /= s;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 0)
    {
        int w = bottom_top_blob.w;
        int h = bottom_top_blob.h;
        int channels = bottom_top_blob.c;
        int size = w * h;

        Mat max;
        max.create(w, h, elemsize, opt.workspace_allocator);
        if (max.empty())
            return -100;
        max.fill(-FLT_MAX);
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                max[i] = std::max(max[i], ptr[i]);
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                ptr[i] = exp(ptr[i] - max[i]);
            }
        }

        Mat sum;
        sum.create(w, h, elemsize, opt.workspace_allocator);
        if (sum.empty())
            return -100;
        sum.fill(0.f);
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                sum[i] += ptr[i];
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                ptr[i] /= sum[i];
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 1)
    {
        int w = bottom_top_blob.w;
        int h = bottom_top_blob.h;
        int channels = bottom_top_blob.c;

        Mat max;
        max.create(h, channels, elemsize, opt.workspace_allocator);
        if (max.empty())
            return -100;
        max.fill(-FLT_MAX);
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);
            float* maxptr = max.row(q);

            for (int i=0; i<h; i++)
            {
                float max = -FLT_MAX;
                for (int j=0; j<w; j++)
                {
                    max = std::max(max, ptr[j]);
                }

                maxptr[i] = max;
                ptr += w;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);
            float* maxptr = max.row(q);

            for (int i=0; i<h; i++)
            {
                float max = maxptr[i];
                for (int j=0; j<w; j++)
                {
                    ptr[j] = exp(ptr[j] - max);
                }

                ptr += w;
            }
        }

        Mat sum;
        sum.create(h, channels, elemsize, opt.workspace_allocator);
        if (sum.empty())
            return -100;
        sum.fill(0.f);
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);
            float* sumptr = sum.row(q);

            for (int i=0; i<h; i++)
            {
                float sum = 0.f;
                for (int j=0; j<w; j++)
                {
                    sum += ptr[j];
                }

                sumptr[i] = sum;
                ptr += w;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);
            float* sumptr = sum.row(q);

            for (int i=0; i<h; i++)
            {
                float sum = sumptr[i];
                for (int j=0; j<w; j++)
                {
                    ptr[j] /= sum;
                }

                ptr += w;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 2)
    {
        int w = bottom_top_blob.w;
        int h = bottom_top_blob.h;
        int channels = bottom_top_blob.c;

        Mat max;
        max.create(w, channels, elemsize, opt.workspace_allocator);
        if (max.empty())
            return -100;
        max.fill(-FLT_MAX);
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);
            float* maxptr = max.row(q);

            for (int i=0; i<h; i++)
            {
                for (int j=0; j<w; j++)
                {
                    maxptr[j] = std::max(maxptr[j], ptr[j]);
                }

                ptr += w;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);
            float* maxptr = max.row(q);

            for (int i=0; i<h; i++)
            {
                for (int j=0; j<w; j++)
                {
                    ptr[j] = exp(ptr[j] - maxptr[j]);
                }

                ptr += w;
            }
        }

        Mat sum;
        sum.create(w, channels, elemsize, opt.workspace_allocator);
        if (sum.empty())
            return -100;
        sum.fill(0.f);
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_top_blob.channel(q);
            float* sumptr = sum.row(q);

            for (int i=0; i<h; i++)
            {
                for (int j=0; j<w; j++)
                {
                    sumptr[j] += ptr[j];
                }

                ptr += w;
            }
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q=0; q<channels; q++)
        {
            float* ptr = bottom_top_blob.channel(q);
            float* sumptr = sum.row(q);

            for (int i=0; i<h; i++)
            {
                for (int j=0; j<w; j++)
                {
                    ptr[j] /= sumptr[j];
                }

                ptr += w;
            }
        }

        return 0;
    }

    return 0;
}

#if NCNN_VULKAN
int Softmax::forward_inplace(VkMat& bottom_top_blob, Command& cmd, const Option& opt) const
{
    int dims = bottom_top_blob.dims;
    int w = bottom_top_blob.w;
    int h = bottom_top_blob.h;
    int channels = bottom_top_blob.c;

    fprintf(stderr, "Softmax::forward_inplace %p\n", bottom_top_blob.buffer);

    VkMat max_workspace;
    VkMat sum_workspace;

    if (dims == 1) // axis == 0
    {
        max_workspace.create(1, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(1, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }
    else if (dims == 2 && axis == 0)
    {
        max_workspace.create(w, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(w, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }
    else if (dims == 2 && axis == 1)
    {
        max_workspace.create(h, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(h, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }
    else if (dims == 3 && axis == 0)
    {
        max_workspace.create(w, h, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(w, h, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }
    else if (dims == 3 && axis == 1)
    {
        max_workspace.create(h, channels, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(h, channels, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }
    else if (dims == 3 && axis == 2)
    {
        max_workspace.create(w, channels, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
        sum_workspace.create(w, channels, 4u, opt.workspace_vkallocator, opt.staging_vkallocator);
    }

    std::vector<VkMat> bindings(3);
    bindings[0] = bottom_top_blob;
    bindings[1] = max_workspace;
    bindings[2] = sum_workspace;

    std::vector<int> constants(5);
    constants[0] = bottom_top_blob.dims;
    constants[1] = bottom_top_blob.w;
    constants[2] = bottom_top_blob.h;
    constants[3] = bottom_top_blob.c;
    constants[4] = bottom_top_blob.cstep;

    uint32_t group_count_xyz[3];
    group_count_xyz[0] = (bottom_top_blob.w + local_size_x - 1) / local_size_x;
    group_count_xyz[1] = (bottom_top_blob.h + local_size_y - 1) / local_size_y;
    group_count_xyz[2] = (bottom_top_blob.c + local_size_z - 1) / local_size_z;

    // record
    cmd.record_bind_pipeline(pipeline);
    cmd.record_update_bindings(pipeline_layout, descriptor_update_template, bindings);
    cmd.record_push_constants(pipeline_layout, constants);
    cmd.record_dispatch(group_count_xyz);

    return 0;
}
#endif // NCNN_VULKAN

} // namespace ncnn
