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

#ifndef ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_SEQUENCE_CONCAT_H
#define ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_SEQUENCE_CONCAT_H

#include "saber/funcs/impl/impl_sequence_concat.h"

namespace anakin{

namespace saber{

template <DataType OpDtype>
class SaberSequenceConcat<NV, OpDtype> :
    public ImplBase<
        NV, OpDtype,
        SequenceConcatParam<NV> > {
public:
    typedef typename DataTrait<NV, OpDtype>::Dtype OpDataType;
    SaberSequenceConcat() = default;
    ~SaberSequenceConcat() = default;

    virtual SaberStatus init(const std::vector<Tensor<NV> *>& inputs,
                            std::vector<Tensor<NV> *>& outputs,
                            SequenceConcatParam<NV>& param, Context<NV>& ctx);

    virtual SaberStatus create(const std::vector<Tensor<NV> *>& inputs,
                            std::vector<Tensor<NV> *>& outputs,
                            SequenceConcatParam<NV>& param, Context<NV> &ctx);
    
    virtual SaberStatus dispatch(const std::vector<Tensor<NV>*>& inputs,
                          std::vector<Tensor<NV>*>& outputs,
                          SequenceConcatParam<NV>& param);
private:
    Tensor<NV> _out2in_map_tensor;
    Tensor<NV> _out2in_word_map_tensor;
    Tensor<NV> _in_locate_tensor;
};

}

}
#endif //ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_SEQUENCE_CONCAT_H
