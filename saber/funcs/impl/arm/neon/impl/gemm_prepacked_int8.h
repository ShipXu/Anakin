/* Copyright (c) 2018 Anakin Authors, Inc. All Rights Reserved.
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef ANAKIN_SABER_FUNCS_IMPL_ARM_NEON_IMPL_GEMM_PREPACKED_INT8_H
#define ANAKIN_SABER_FUNCS_IMPL_ARM_NEON_IMPL_GEMM_PREPACKED_INT8_H

#include "saber/core/context.h"
#include "saber/core/tensor.h"
namespace anakin{
namespace saber{

#ifdef __aarch64__
// for int7/int8 gemm
// const int HBLOCK = 4;
// const int NBLOCK = 16;
const int MBLOCK_INT8 = 4;
const int KBLOCK_INT8 = 4;
const int NBLOCK_INT8 = 16;

inline int get_hblock_int8(ARMArch arch) {
    return MBLOCK_INT8;
}
#else
//const int HBLOCK = 4;
//const int WBLOCK = 8;
const int MBLOCK_INT8 = 4;
const int KBLOCK_INT8 = 4;
const int NBLOCK_INT8 = 8;

inline int get_hblock_int8(ARMArch arch) {
    return 4;
}
#endif// __aarch64__

void prepackA_int8(void* out, const void* in, const int ldin, const int m0, \
    const int mmax, const int k0, const int kmax, bool is_trans);

void prepackA_int8(Tensor<ARM>& tout, const Tensor<ARM>& tin, \
    int m, int k, int group, bool is_trans, Context<ARM>* ctx);

template <typename dtype>
void gemm_prepack_int8(const int8_t* A_packed, const int8_t* B, const int* bias, dtype* C, \
    int M, int N, int K, bool is_bias, bool is_relu, bool is_transB, \
    const float* scale, Context<ARM>* ctx);

#define ROUNDUP(a, b) (((a + b - 1) / b) * b)

} //namespace saber
} //namespace anakin

#endif //ANAKIN_SABER_FUNCS_IMPL_ARM_NEON_IMPL_GEMM_PREPACKED_INT8_H
