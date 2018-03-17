/*!
 *  Copyright (c) 2017 by Contributors
 * \file matrix_op.cc
 * \brief Matrix operators
 */
#include <nnvm/op.h>
#include <nnvm/node.h>
#include <nnvm/op_attr_types.h>
#include <nnvm/compiler/op_attr_types.h>
#include <nnvm/top/tensor.h>
#include "topi/nn.h"
#include "../op_common.h"
#include "../elemwise_op_common.h"

namespace nnvm {
namespace top {

using namespace tvm;
using namespace nnvm::compiler;

DMLC_REGISTER_PARAMETER(MatMulParam);

inline bool DotShape(const nnvm::NodeAttrs& attrs,
                     std::vector<TShape> *in_attrs,
                     std::vector<TShape> *out_attrs) {
  const auto& param = nnvm::get<MatMulParam>(attrs.parsed);
  CHECK_EQ(in_attrs->size(), 2U);
  CHECK_EQ(out_attrs->size(), 1U);
  TShape lshape = (*in_attrs)[0];
  TShape rshape = (*in_attrs)[1];

  if (lshape.ndim() == 1)  lshape = TShape{1, lshape[0]};
  if (rshape.ndim() == 1) rshape = TShape{rshape[0], 1};

  if (param.transpose_a) std::reverse(lshape.begin(), lshape.end());
  if (param.transpose_b) std::reverse(rshape.begin(), rshape.end());

  CHECK_EQ(lshape[lshape.ndim() - 1], rshape[0])
    << "shape not aligned: " << lshape << " X " << rshape;

  TShape oshape(lshape.ndim() + rshape.ndim() - 2);
  for (int i = 0; i < lshape.ndim() - 1; i++) oshape[i] = lshape[i];
  for (int i = 1; i < rshape.ndim(); i++) oshape[i + lshape.ndim() - 2] = rshape[i];

  NNVM_ASSIGN_OUTPUT_SHAPE(attrs, *out_attrs, 0, oshape);
  return true;
}

NNVM_REGISTER_OP(matmul)
  .describe(R"doc(Matrix multiplication of two matrices.

.. math::
   A(i, k) * B(k, j), if trans_a == trans_b

)doc" NNVM_ADD_FILELINE)
.set_support_level(1)
.set_num_inputs(2)
.set_num_outputs(1)
.set_attr_parser(ParamParser<MatMulParam>)
.set_attr<FGetAttrDict>("FGetAttrDict", ParamGetAttrDict<MatMulParam>)
.add_arguments(MatMulParam::__FIELDS__())
.add_argument("lhs", "NDArray-or-Symbol", "The first input")
.add_argument("rhs", "NDArray-or-Symbol", "The second input")
.set_attr<FInferShape>("FInferShape", DotShape)
.set_attr<FInferType>("FInferType", ElemwiseType<2, 1>)
.set_attr<FTVMCompute>(
  "FTVMCompute", [](const NodeAttrs& attrs,
                    const Array<Tensor>& inputs,
                    const Array<Tensor>& out_info) {
    const auto& param = nnvm::get<MatMulParam>(attrs.parsed);
    return Array<Tensor>{topi::matmult(inputs[0],
                                       inputs[1],
                                       param.transpose_a,
                                       param.transpose_b)};
  })
.set_attr<FGradient>(
  "FGradient", [](const NodePtr& n,
                  const std::vector<NodeEntry>& ograds) {
    // z = x dot y
    // xshape (n,m,k), yshape (k,r,s)
    const MatMulParam& param = nnvm::get<MatMulParam>(n->attrs.parsed);
    bool Ta = param.transpose_a;
    bool Tb = param.transpose_b;
    // Ta = false, Tb = false
    // grad_x = grad_z dot y.T
    // grad_y = x.T dot grad_z
    if (!Ta && !Tb) {
      return std::vector<NodeEntry>{
        MakeNode("matmul", n->attrs.name + "_grad_0",
                 {ograds[0], n->inputs[1]},
                 {{"transpose_a", "false"},
                  {"transpose_b", "true"}}),
        MakeNode("matmul", n->attrs.name + "_grad_1",
                 {n->inputs[0], ograds[0]},
                 {{"transpose_a", "true"},
                  {"transpose_b", "false"}})
      };
    } else if (Ta && !Tb) {
      // Ta = true, Tb = false
      // grad_x = y dot grad_z.T
      // grad_y = x dot grad_z
      return std::vector<NodeEntry>{
        MakeNode("matmul", n->attrs.name + "_grad_0",
                 {n->inputs[1], ograds[0]},
                 {{"transpose_a", "false"},
                  {"transpose_b", "true"}}),
        MakeNode("matmul", n->attrs.name + "_grad_1",
                 {n->inputs[0], ograds[0]},
                 {{"transpose_a", "false"},
                  {"transpose_b", "false"}})
      };
    } else if (!Ta && Tb) {
      // Ta = false, Tb = true
      // grad_x = grad_z dot y
      // grad_y = grad_z.T dot x
      return std::vector<NodeEntry>{
        MakeNode("matmul", n->attrs.name + "_grad_0",
                 {ograds[0], n->inputs[1]},
                 {{"transpose_a", "false"},
                  {"transpose_b", "false"}}),
        MakeNode("matmul", n->attrs.name + "_grad_1",
                 {ograds[0], n->inputs[0]},
                 {{"transpose_a", "true"},
                  {"transpose_b", "false"}})
      };
    } else {
      // Ta = true, Tb = true
      // grad_x = y.T dot grad_z.T
      // grad_y = grad_z.T dot x.T
      return std::vector<NodeEntry>{
        MakeNode("matmul", n->attrs.name + "_grad_0",
                 {n->inputs[1], ograds[0]},
                 {{"transpose_a", "true"},
                  {"transpose_b", "true"}}),
        MakeNode("matmul", n->attrs.name + "_grad_1",
                 {ograds[0], n->inputs[0]},
                 {{"transpose_a", "true"},
                  {"transpose_b", "true"}})
      };
    }
});
}  // namespace top
}  // namespace nnvm
