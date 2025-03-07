/* ************************************************************************
 * Copyright (C) 2021-2022 Advanced Micro Devices, Inc. All rights reserved.
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

#include "testing_common.hpp"

inline hipblasStatus_t testing_bad_operation()
{
    Arguments          arg;
    hipblasLocalHandle handle(arg);
    // invalid transpose operation enum
    return hipblasSgemv(
        handle, hipblasOperation_t(0), 0, 0, nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr, 0);
}

inline hipblasStatus_t testing_handle()
{
    // Test out hipblasCreate() and hipblasDestroy()
    hipblasHandle_t handle;
    CHECK_HIPBLAS_ERROR(hipblasCreate(&handle));
    CHECK_HIPBLAS_ERROR(hipblasDestroy(handle));
    return HIPBLAS_STATUS_SUCCESS;
}
