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

using hipblasTbsvStridedBatchedModel = ArgumentModel<e_uplo,
                                                     e_transA,
                                                     e_diag,
                                                     e_M,
                                                     e_K,
                                                     e_lda,
                                                     e_incx,
                                                     e_stride_scale,
                                                     e_batch_count>;

inline void testname_tbsv_strided_batched(const Arguments& arg, std::string& name)
{
    hipblasTbsvStridedBatchedModel{}.test_name(arg, name);
}

template <typename T>
inline hipblasStatus_t testing_tbsv_strided_batched(const Arguments& arg)
{
    bool FORTRAN = arg.fortran;
    auto hipblasTbsvStridedBatchedFn
        = FORTRAN ? hipblasTbsvStridedBatched<T, true> : hipblasTbsvStridedBatched<T, false>;

    hipblasFillMode_t  uplo         = char2hipblas_fill(arg.uplo);
    hipblasDiagType_t  diag         = char2hipblas_diagonal(arg.diag);
    hipblasOperation_t transA       = char2hipblas_operation(arg.transA);
    int                M            = arg.M;
    int                K            = arg.K;
    int                incx         = arg.incx;
    int                lda          = arg.lda;
    double             stride_scale = arg.stride_scale;
    int                batch_count  = arg.batch_count;

    int           abs_incx = incx < 0 ? -incx : incx;
    hipblasStride strideA  = size_t(M) * M;
    hipblasStride strideAB = size_t(M) * lda * stride_scale;
    hipblasStride stridex  = size_t(abs_incx) * M * stride_scale;
    size_t        size_A   = strideA * batch_count;
    size_t        size_AB  = strideAB * batch_count;
    size_t        size_x   = stridex * batch_count;

    hipblasLocalHandle handle(arg);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    bool invalid_size = M < 0 || K < 0 || lda < K + 1 || !incx || batch_count < 0;
    if(invalid_size || !M || !batch_count)
    {
        hipblasStatus_t actual = hipblasTbsvStridedBatchedFn(handle,
                                                             uplo,
                                                             transA,
                                                             diag,
                                                             M,
                                                             K,
                                                             nullptr,
                                                             lda,
                                                             strideA,
                                                             nullptr,
                                                             incx,
                                                             stridex,
                                                             batch_count);
        EXPECT_HIPBLAS_STATUS(
            actual, (invalid_size ? HIPBLAS_STATUS_INVALID_VALUE : HIPBLAS_STATUS_SUCCESS));
        return actual;
    }

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory
    host_vector<T> hA(size_A);
    host_vector<T> hAB(size_AB);
    host_vector<T> AAT(size_A);
    host_vector<T> hb(size_x);
    host_vector<T> hx(size_x);
    host_vector<T> hx_or_b_1(size_x);

    device_vector<T> dAB(size_AB);
    device_vector<T> dx_or_b(size_x);

    double gpu_time_used, hipblas_error, cumulative_hipblas_error = 0;

    // Initial Data on CPU
    hipblas_init_matrix(hA, arg, M, M, M, strideA, batch_count, hipblas_client_never_set_nan, true);
    hipblas_init_vector(
        hx, arg, M, abs_incx, stridex, batch_count, hipblas_client_never_set_nan, false, true);
    hb = hx;

    for(int b = 0; b < batch_count; b++)
    {
        T* hAbat  = hA.data() + b * strideA;
        T* hABbat = hAB.data() + b * strideAB;
        T* AATbat = AAT.data() + b * strideA;
        T* hbbat  = hb.data() + b * stridex;
        banded_matrix_setup(uplo == HIPBLAS_FILL_MODE_UPPER, hAbat, M, M, K);

        prepare_triangular_solve(hAbat, M, AATbat, M, arg.uplo);
        if(diag == HIPBLAS_DIAG_UNIT)
        {
            make_unit_diagonal(uplo, hAbat, M, M);
        }

        regular_to_banded(uplo == HIPBLAS_FILL_MODE_UPPER, hAbat, M, hABbat, lda, M, K);

        // Calculate hb = hA*hx;
        cblas_tbmv<T>(uplo, transA, diag, M, K, hABbat, lda, hbbat, incx);
    }

    hx_or_b_1 = hb;

    // copy data from CPU to device
    CHECK_HIP_ERROR(hipMemcpy(dAB, hAB.data(), sizeof(T) * size_AB, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(
        hipMemcpy(dx_or_b, hx_or_b_1.data(), sizeof(T) * size_x, hipMemcpyHostToDevice));

    /* =====================================================================
           HIPBLAS
    =================================================================== */
    if(arg.unit_check || arg.norm_check)
    {
        CHECK_HIPBLAS_ERROR(hipblasTbsvStridedBatchedFn(handle,
                                                        uplo,
                                                        transA,
                                                        diag,
                                                        M,
                                                        K,
                                                        dAB,
                                                        lda,
                                                        strideAB,
                                                        dx_or_b,
                                                        incx,
                                                        stridex,
                                                        batch_count));

        // copy output from device to CPU
        CHECK_HIP_ERROR(
            hipMemcpy(hx_or_b_1.data(), dx_or_b, sizeof(T) * size_x, hipMemcpyDeviceToHost));

        // Calculating error
        // For norm_check/bench, currently taking the cumulative sum of errors over all batches
        for(int b = 0; b < batch_count; b++)
        {
            hipblas_error = std::abs(vector_norm_1<T>(
                M, abs_incx, hx.data() + b * stridex, hx_or_b_1.data() + b * stridex));
            if(arg.unit_check)
            {
                double tolerance = std::numeric_limits<real_t<T>>::epsilon() * 40 * M;
                unit_check_error(hipblas_error, tolerance);
            }

            cumulative_hipblas_error += hipblas_error;
        }
    }

    if(arg.timing)
    {
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasTbsvStridedBatchedFn(handle,
                                                            uplo,
                                                            transA,
                                                            diag,
                                                            M,
                                                            K,
                                                            dAB,
                                                            lda,
                                                            strideAB,
                                                            dx_or_b,
                                                            incx,
                                                            stridex,
                                                            batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used; // in microseconds

        hipblasTbsvStridedBatchedModel{}.log_args<T>(std::cout,
                                                     arg,
                                                     gpu_time_used,
                                                     tbsv_gflop_count<T>(M, K),
                                                     tbsv_gbyte_count<T>(M, K),
                                                     cumulative_hipblas_error);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
