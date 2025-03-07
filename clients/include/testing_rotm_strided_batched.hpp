/* ************************************************************************
 * Copyright (C) 2016-2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ************************************************************************ */

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

/* ============================================================================================ */

using hipblasRotmStridedBatchedModel
    = ArgumentModel<e_N, e_incx, e_incy, e_stride_scale, e_batch_count>;

inline void testname_rotm_strided_batched(const Arguments& arg, std::string& name)
{
    hipblasRotmStridedBatchedModel{}.test_name(arg, name);
}

template <typename T>
inline hipblasStatus_t testing_rotm_strided_batched(const Arguments& arg)
{
    bool FORTRAN = arg.fortran;
    auto hipblasRotmStridedBatchedFn
        = FORTRAN ? hipblasRotmStridedBatched<T, true> : hipblasRotmStridedBatched<T, false>;

    double stride_scale = arg.stride_scale;

    int           N            = arg.N;
    int           incx         = arg.incx;
    int           incy         = arg.incy;
    hipblasStride stride_param = 5 * stride_scale;
    int           batch_count  = arg.batch_count;

    const T rel_error = std::numeric_limits<T>::epsilon() * 1000;

    hipblasLocalHandle handle(arg);

    int           abs_incx = incx >= 0 ? incx : -incx;
    int           abs_incy = incy >= 0 ? incy : -incy;
    hipblasStride stride_x = N * abs_incx * stride_scale;
    hipblasStride stride_y = N * abs_incy * stride_scale;

    // check to prevent undefined memory allocation error
    if(N <= 0 || batch_count <= 0)
    {
        CHECK_HIPBLAS_ERROR((hipblasRotmStridedBatchedFn(handle,
                                                         N,
                                                         nullptr,
                                                         incx,
                                                         stride_x,
                                                         nullptr,
                                                         incy,
                                                         stride_y,
                                                         nullptr,
                                                         stride_param,
                                                         batch_count)));

        return HIPBLAS_STATUS_SUCCESS;
    }

    double gpu_time_used, hipblas_error_device;

    size_t size_x     = N * size_t(abs_incx) + size_t(stride_x) * size_t(batch_count - 1);
    size_t size_y     = N * size_t(abs_incy) + size_t(stride_y) * size_t(batch_count - 1);
    size_t size_param = 5 + size_t(stride_param) * size_t(batch_count - 1);
    if(!size_x)
        size_x = 1;
    if(!size_y)
        size_y = 1;

    device_vector<T> dx(size_x);
    device_vector<T> dy(size_y);
    device_vector<T> dparam(size_param);

    // Initial Data on CPU
    host_vector<T> hx(size_x);
    host_vector<T> hy(size_y);
    host_vector<T> hdata(4 * batch_count);
    host_vector<T> hparam(size_param);

    hipblas_init_vector(
        hx, arg, N, abs_incx, stride_x, batch_count, hipblas_client_alpha_sets_nan, true);
    hipblas_init_vector(
        hy, arg, N, abs_incy, stride_y, batch_count, hipblas_client_alpha_sets_nan, false);
    hipblas_init_vector(hdata, arg, 4, 1, 4, batch_count, hipblas_client_alpha_sets_nan, false);

    for(int b = 0; b < batch_count; b++)
        cblas_rotmg<T>(hdata + b * 4,
                       hdata + b * 4 + 1,
                       hdata + b * 4 + 2,
                       hdata + b * 4 + 3,
                       hparam + b * stride_param);

    constexpr int FLAG_COUNT        = 4;
    const T       FLAGS[FLAG_COUNT] = {-1, 0, 1, -2};

    for(int i = 0; i < FLAG_COUNT; i++)
    {
        if(arg.unit_check || arg.norm_check)
        {
            for(int b = 0; b < batch_count; b++)
                (hparam + b * stride_param)[0] = FLAGS[i];

            // Test device
            CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
            CHECK_HIP_ERROR(hipMemcpy(dx, hx, sizeof(T) * size_x, hipMemcpyHostToDevice));
            CHECK_HIP_ERROR(hipMemcpy(dy, hy, sizeof(T) * size_y, hipMemcpyHostToDevice));
            CHECK_HIP_ERROR(
                hipMemcpy(dparam, hparam, sizeof(T) * size_param, hipMemcpyHostToDevice));
            CHECK_HIPBLAS_ERROR((hipblasRotmStridedBatchedFn(handle,
                                                             N,
                                                             dx,
                                                             incx,
                                                             stride_x,
                                                             dy,
                                                             incy,
                                                             stride_y,
                                                             dparam,
                                                             stride_param,
                                                             batch_count)));

            host_vector<T> rx(size_x);
            host_vector<T> ry(size_y);
            CHECK_HIP_ERROR(hipMemcpy(rx, dx, sizeof(T) * size_x, hipMemcpyDeviceToHost));
            CHECK_HIP_ERROR(hipMemcpy(ry, dy, sizeof(T) * size_y, hipMemcpyDeviceToHost));

            host_vector<T> cx = hx;
            host_vector<T> cy = hy;

            // CPU BLAS reference data
            for(int b = 0; b < batch_count; b++)
            {
                cblas_rotm<T>(
                    N, cx + b * stride_x, incx, cy + b * stride_y, incy, hparam + b * stride_param);
            }

            if(arg.unit_check)
            {
                near_check_general<T>(1, N, batch_count, abs_incx, stride_x, cx, rx, rel_error);
                near_check_general<T>(1, N, batch_count, abs_incy, stride_y, cy, ry, rel_error);
            }
            if(arg.norm_check)
            {
                hipblas_error_device
                    = norm_check_general<T>('F', 1, N, abs_incx, stride_x, cx, rx, batch_count);
                hipblas_error_device
                    += norm_check_general<T>('F', 1, N, abs_incy, stride_y, cy, ry, batch_count);
            }
        }
    }

    if(arg.timing)
    {
        for(int b = 0; b < batch_count; b++)
            (hparam + b * stride_param)[0] = 0;
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIP_ERROR(hipMemcpy(dx, hx, sizeof(T) * size_x, hipMemcpyHostToDevice));
        CHECK_HIP_ERROR(hipMemcpy(dy, hy, sizeof(T) * size_y, hipMemcpyHostToDevice));
        CHECK_HIP_ERROR(hipMemcpy(dparam, hparam, sizeof(T) * size_param, hipMemcpyHostToDevice));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR((hipblasRotmStridedBatchedFn(handle,
                                                             N,
                                                             dx,
                                                             incx,
                                                             stride_x,
                                                             dy,
                                                             incy,
                                                             stride_y,
                                                             dparam,
                                                             stride_param,
                                                             batch_count)));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        hipblasRotmStridedBatchedModel{}.log_args<T>(std::cout,
                                                     arg,
                                                     gpu_time_used,
                                                     rotm_gflop_count<T>(N, 0),
                                                     rotm_gbyte_count<T>(N, 0),
                                                     0,
                                                     hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
