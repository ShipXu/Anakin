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

#ifndef ANAKIN_SABER_FUNCS_BATCH_GEMM_H
#define ANAKIN_SABER_FUNCS_BATCH_GEMM_H

#include "anakin_config.h"
#include "saber/core/context.h"
#include "saber/saber_types.h"

namespace anakin {
namespace saber {

template <typename TargetType,
        typename inDtype,
        typename outDtype = inDtype>
class BatchMatrixFunc {
public:
    virtual SaberStatus init(
            const bool trans_A, const bool trans_B,
            const int max_batch,
            Context<TargetType> ctx) = 0;

    virtual SaberStatus dispatch(
            const outDtype alpha, const outDtype beta,
            const inDtype* a[], const inDtype* b[],
            const int m, const int n, const int k,
            outDtype* c[], const int batch) = 0;

    virtual SaberStatus dispatch(
            const outDtype alpha, const outDtype beta,
            inDtype* a[],
            const int m, const int n, const int k,
            const int batch) = 0;
};

template<typename TargetType,
        ImplEnum impl,
        typename inDtype,
        typename outDtype = inDtype>
class BatchGemm : public BatchMatrixFunc<TargetType, inDtype, outDtype> {
    // Row major gemm
public:
    BatchGemm() = default;
    ~BatchGemm() = default;

    SaberStatus init(const bool trans_A, const bool trans_B,
                     const int max_batch,
                     Context<TargetType> ctx) {
        return SaberUnImplError;
    }

    SaberStatus dispatch(const outDtype alpha, const outDtype beta,
                         const inDtype* a[], const inDtype* b[],
                         const int m, const int n, const int k,
                         outDtype* c[], const int batch) {
        return SaberUnImplError;
    }
    SaberStatus dispatch(const outDtype alpha, const outDtype beta,
                         inDtype* a[],
                         const int m, const int n, const int k,
                         const int batch) {
        return SaberUnImplError;
    }

private:
    Context<TargetType> _ctx;
};

}
}

#ifdef USE_CUDA
#include "saber/funcs/impl/cuda/vender_batch_gemm.h"
#endif

#ifdef USE_X86_PLACE
//#include "saber/funcs/impl/x86/vender_batch_gemm.h"
#endif

#endif
