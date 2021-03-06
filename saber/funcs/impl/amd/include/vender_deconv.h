/* Copyright (c) 2019 Anakin Authors, Inc. All Rights Reserved.

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

#pragma once

#include "saber/funcs/impl/impl_deconv.h"
#include "saber/core/impl/amd/utils/amd_base.h"
#include "saber/funcs/base.h"
#include "saber/core/impl/amd/utils/amd_kernel.h"
#include "saber/funcs/funcs_utils.h"
#include "saber/funcs/impl/amd/include/amd_utils.h"
#include <miopengemm/miogemm.hpp>
#include <miopengemm/gemm.hpp>
#include <miopengemm/geometry.hpp>

namespace anakin {

namespace saber {

template <DataType OpDtype>
class VenderDeconv2D<AMD, OpDtype> :
    public ImplBase<AMD, OpDtype, ConvParam<AMD> > {
public:
    typedef typename DataTrait<AMD, OpDtype>::Dtype OpDataType;
    typedef AMD_API::TPtr PtrDtype;

    typedef ImplBase<AMD, OpDtype, ConvParam<AMD> > Impl_t;
    VenderDeconv2D()
        : _use_gemm(false)
        , _multikernel(false)
        , _outGemmWorkspace(nullptr)
    {
        _kernels_ptr.clear();
    }

    ~VenderDeconv2D() {
        if (_outGemmWorkspace) {
            delete _outGemmWorkspace;
        }

        _kernels_ptr.clear();
    }

    virtual SaberStatus init(const std::vector<Tensor<AMD> *>& inputs,
                             std::vector<Tensor<AMD> *>& outputs,
                             ConvParam<AMD>& param, Context<AMD>& ctx) override;

    virtual SaberStatus create(const std::vector<Tensor<AMD> *>& inputs,
                               std::vector<Tensor<AMD> *>& outputs,
                               ConvParam<AMD>& param, Context<AMD>& ctx) override;

    virtual SaberStatus dispatch(const std::vector<Tensor<AMD>*>& inputs,
                                 std::vector<Tensor<AMD>*>& outputs,
                                 ConvParam<AMD>& param) override;

    SaberStatus trans_weights(Tensor<AMD>& target_weights,
                              Tensor<AMD>& target_bias,
                              int in_channel, int out_channel,
                              int stride_h, int stride_w,
                              int pad_h, int pad_w,
                              int dilation_h, int dilation_w,
                              int group);
private:

    std::vector<AMDKernelPtr> _kernels_ptr;
    void CreateKernelList(int device_id, KernelInfo& kernelInfo);
    SaberStatus create_gemm(const std::vector<Tensor<AMD> *>& inputs,
                            std::vector<Tensor<AMD> *>& outputs,
                            ConvParam<AMD>& param, Context<AMD>& ctx);
    SaberStatus dispatch_gemm(const std::vector<Tensor<AMD>*>& inputs,
                              std::vector<Tensor<AMD>*>& outputs,
                              ConvParam<AMD>& param,
                              amd_kernel_list& list);

    bool _use_gemm;
    AMDKernelPtr _kernel_atomic;
    AMDKernelPtr _kernel_normal;
    AMDKernelPtr _kernel_col2Im;
    AMDKernelPtr _kernel_col2Im_bias_relu;
    AMDKernelPtr _kernel_isBias;

    bool _multikernel;
    Tensor<AMD>* _outGemmWorkspace;

};

//template class SaberDeconv2D<AMD, AK_FLOAT>;
} //namespace saber

} //namespace anakin
