#include "../common/cnnl_scalar.hpp"
#include "../common/common.hpp"
#include "../common/float16.hpp"
#include "../diopi_helper.hpp"
#include "adam.hpp"

namespace impl {
namespace camb {

extern "C" diopiError_t diopiAdam(diopiContextHandle_t ctx, diopiTensorHandle_t input, diopiTensorHandle_t grad, diopiTensorHandle_t exp_avg,
                                  diopiTensorHandle_t exp_avg_sq, diopiTensorHandle_t max_exp_avg_sq, float lr, float beta1, float beta2, float eps,
                                  float weight_decay, int64_t step, bool amsgrad) {
    cnrtQueue_t queue = getStream(ctx);
    DiopiTensor input_tensor = DiopiTensor(input);
    DiopiTensor grad_tensor = DiopiTensor(grad);
    DiopiTensor exp_avg_tensor = DiopiTensor(exp_avg);
    DiopiTensor exp_avg_sq_tensor = DiopiTensor(exp_avg_sq);
    DiopiTensor max_exp_avg_sq_tensor = DiopiTensor(max_exp_avg_sq);

    DiopiTensor input_casted = input_tensor;
    DiopiTensor grad_casted = grad_tensor;
    DiopiTensor exp_avg_casted = exp_avg_tensor;
    DiopiTensor exp_avg_sq_casted = exp_avg_sq_tensor;
    DiopiTensor max_exp_avg_sq_casted = max_exp_avg_sq_tensor;

    std::vector<DiopiTensor *> tensors{&input_casted, &grad_casted, &exp_avg_casted, &exp_avg_sq_casted, &max_exp_avg_sq_casted};
    DIOPI_CALL(autoCastTensorType(ctx, tensors, {diopi_dtype_float16, diopi_dtype_float32}));

    float beta1_correction_recip = 1;
    float beta2_correction_recip = 1;
    beta1_correction_recip = 1 / (1 - std::pow(beta1, step));
    beta2_correction_recip = 1 / (1 - std::pow(beta2, step));
    float epsilon_correction = eps / std::sqrt(beta2_correction_recip);
    float learning_rate_correction = lr * beta1_correction_recip / std::sqrt(beta2_correction_recip);
    
    int adamw_mode = 0;
    float decay_correction = 1 - lr * weight_decay ; 
    cnrtDim3_t k_dim;
    int cluster_count = 0;
    int core_per_cluster = 0;
    cnrtDeviceGetAttribute(&cluster_count, cnrtAttrClusterCount, 0);
    cnrtDeviceGetAttribute(&core_per_cluster, cnrtAttrMcorePerCluster, 0);
    k_dim.x = core_per_cluster;
    k_dim.y = cluster_count;
    k_dim.z = 1;
    cnrtFunctionType_t k_type = CNRT_FUNC_TYPE_UNION1;
    cnrtDataType_t cnrt_type;
    if(input_casted.dtype() == diopi_dtype_float32){
        cnrt_type = cnrtFloat32;
    }else{
        cnrt_type = cnrtFloat16;
    }
    
    bang_fused_adam_internal(
        grad_casted.data(),
        exp_avg_casted.data(),
        exp_avg_sq_casted.data(),
        max_exp_avg_sq_casted.data(),
        input_casted.data(),
        input_casted.numel(),
        1,
        beta1,
        beta2,
        epsilon_correction,
        learning_rate_correction,
        adamw_mode,
        weight_decay,
        decay_correction,
        k_dim,
        k_type,
        queue,
        cnrt_type,
        amsgrad
    );
    DIOPI_CALL(dataTypeCast(ctx, grad_tensor, grad_casted));
    DIOPI_CALL(dataTypeCast(ctx, input_tensor, input_casted));
    DIOPI_CALL(dataTypeCast(ctx, exp_avg_tensor, exp_avg_casted));
    DIOPI_CALL(dataTypeCast(ctx, exp_avg_sq_tensor, exp_avg_sq_casted));
    DIOPI_CALL(dataTypeCast(ctx, max_exp_avg_sq_tensor, max_exp_avg_sq_casted));
    return diopiSuccess;
}
}  // namespace camb
}  // namespace impl
