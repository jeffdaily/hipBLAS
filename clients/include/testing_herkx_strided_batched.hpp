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

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

/* ============================================================================================ */

using hipblasHerkxStridedBatchedModel = ArgumentModel<e_uplo,
                                                      e_transA,
                                                      e_N,
                                                      e_K,
                                                      e_alpha,
                                                      e_lda,
                                                      e_ldb,
                                                      e_beta,
                                                      e_ldc,
                                                      e_stride_scale,
                                                      e_batch_count>;

inline void testname_herkx_strided_batched(const Arguments& arg, std::string& name)
{
    hipblasHerkxStridedBatchedModel{}.test_name(arg, name);
}

template <typename T>
inline hipblasStatus_t testing_herkx_strided_batched(const Arguments& arg)
{
    using U                           = real_t<T>;
    bool FORTRAN                      = arg.fortran;
    auto hipblasHerkxStridedBatchedFn = FORTRAN ? hipblasHerkxStridedBatched<T, U, true>
                                                : hipblasHerkxStridedBatched<T, U, false>;

    int    N            = arg.N;
    int    K            = arg.K;
    int    lda          = arg.lda;
    int    ldb          = arg.ldb;
    int    ldc          = arg.ldc;
    double stride_scale = arg.stride_scale;
    int    batch_count  = arg.batch_count;

    hipblasFillMode_t  uplo     = char2hipblas_fill(arg.uplo);
    hipblasOperation_t transA   = char2hipblas_operation(arg.transA);
    int                K1       = (transA == HIPBLAS_OP_N ? K : N);
    hipblasStride      stride_A = size_t(lda) * K1 * stride_scale;
    hipblasStride      stride_B = size_t(ldb) * K1 * stride_scale;
    hipblasStride      stride_C = size_t(ldc) * N * stride_scale;
    size_t             A_size   = stride_A * batch_count;
    size_t             B_size   = stride_B * batch_count;
    size_t             C_size   = stride_C * batch_count;

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(N < 0 || K < 0 || ldc < N || (transA == HIPBLAS_OP_N && (lda < N || ldb < N))
       || (transA != HIPBLAS_OP_N && (lda < K || ldb < K)) || batch_count < 0)
    {
        return HIPBLAS_STATUS_INVALID_VALUE;
    }
    else if(batch_count == 0)
    {
        return HIPBLAS_STATUS_SUCCESS;
    }

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory
    host_vector<T> hA(A_size);
    host_vector<T> hB(B_size);
    host_vector<T> hC_host(C_size);
    host_vector<T> hC_device(C_size);
    host_vector<T> hC_gold(C_size);

    device_vector<T> dA(A_size);
    device_vector<T> dB(B_size);
    device_vector<T> dC(C_size);
    device_vector<T> d_alpha(1);
    device_vector<U> d_beta(1);

    T h_alpha = arg.get_alpha<T>();
    U h_beta  = arg.get_beta<U>();

    double             gpu_time_used, hipblas_error_host, hipblas_error_device;
    hipblasLocalHandle handle(arg);

    // Initial Data on CPU
    srand(1);
    hipblas_init<T>(hA, N, K1, lda, stride_A, batch_count);
    hipblas_init<T>(hB, N, K1, ldb, stride_B, batch_count);
    hipblas_init<T>(hC_host, N, N, ldc, stride_C, batch_count);
    hC_device = hC_host;
    hC_gold   = hC_host;

    // copy data from CPU to device
    CHECK_HIP_ERROR(hipMemcpy(dA, hA, sizeof(T) * A_size, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(dB, hB, sizeof(T) * B_size, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(dC, hC_host, sizeof(T) * C_size, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(d_alpha, &h_alpha, sizeof(T), hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(d_beta, &h_beta, sizeof(U), hipMemcpyHostToDevice));

    if(arg.unit_check || arg.norm_check)
    {
        /* =====================================================================
            HIPBLAS
        =================================================================== */
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
        CHECK_HIPBLAS_ERROR(hipblasHerkxStridedBatchedFn(handle,
                                                         uplo,
                                                         transA,
                                                         N,
                                                         K,
                                                         &h_alpha,
                                                         dA,
                                                         lda,
                                                         stride_A,
                                                         dB,
                                                         ldb,
                                                         stride_B,
                                                         &h_beta,
                                                         dC,
                                                         ldc,
                                                         stride_C,
                                                         batch_count));

        // copy output from device to CPU
        CHECK_HIP_ERROR(hipMemcpy(hC_host, dC, sizeof(T) * C_size, hipMemcpyDeviceToHost));
        CHECK_HIP_ERROR(hipMemcpy(dC, hC_device, sizeof(T) * C_size, hipMemcpyHostToDevice));

        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIPBLAS_ERROR(hipblasHerkxStridedBatchedFn(handle,
                                                         uplo,
                                                         transA,
                                                         N,
                                                         K,
                                                         d_alpha,
                                                         dA,
                                                         lda,
                                                         stride_A,
                                                         dB,
                                                         ldb,
                                                         stride_B,
                                                         d_beta,
                                                         dC,
                                                         ldc,
                                                         stride_C,
                                                         batch_count));

        CHECK_HIP_ERROR(hipMemcpy(hC_device, dC, sizeof(T) * C_size, hipMemcpyDeviceToHost));

        /* =====================================================================
           CPU BLAS
        =================================================================== */
        for(int b = 0; b < batch_count; b++)
        {
            cblas_herkx<T>(uplo,
                           transA,
                           N,
                           K,
                           h_alpha,
                           hA.data() + b * stride_A,
                           lda,
                           hB.data() + b * stride_B,
                           ldb,
                           h_beta,
                           hC_gold.data() + b * stride_C,
                           ldc);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(arg.unit_check)
        {
            unit_check_general<T>(N, N, batch_count, ldc, stride_C, hC_gold, hC_host);
            unit_check_general<T>(N, N, batch_count, ldc, stride_C, hC_gold, hC_device);
        }

        if(arg.norm_check)
        {
            hipblas_error_host
                = norm_check_general<T>('F', N, N, ldc, stride_C, hC_gold, hC_host, batch_count);
            hipblas_error_device
                = norm_check_general<T>('F', N, N, ldc, stride_C, hC_gold, hC_device, batch_count);
        }
    }

    if(arg.timing)
    {
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasHerkxStridedBatchedFn(handle,
                                                             uplo,
                                                             transA,
                                                             N,
                                                             K,
                                                             d_alpha,
                                                             dA,
                                                             lda,
                                                             stride_A,
                                                             dB,
                                                             ldb,
                                                             stride_B,
                                                             d_beta,
                                                             dC,
                                                             ldc,
                                                             stride_C,
                                                             batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used; // in microseconds

        hipblasHerkxStridedBatchedModel{}.log_args<T>(std::cout,
                                                      arg,
                                                      gpu_time_used,
                                                      herkx_gflop_count<T>(N, K),
                                                      herkx_gbyte_count<T>(N, K),
                                                      hipblas_error_host,
                                                      hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
