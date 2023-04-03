#include <diopi/functions.h>

#include <vector>

#include "../cnnl_helper.hpp"
#include "../common/common.hpp"

namespace impl {
namespace camb {
extern "C" {

DIOPI_API diopiError_t
diopiDropout(diopiContextHandle_t ctx, diopiTensorHandle_t out, diopiTensorHandle_t mask, diopiConstTensorHandle_t input, double p, bool train) {
    if (train) {
        cnnlHandle_t handle = cnnlHandlePool.get(ctx);
        auto input_tensor = DiopiTensor(input);
        auto output_tensor = DiopiTensor(out);
        auto mask_tensor = DiopiTensor(mask);

        // Do this Check to use DIOPI-TEST because non-float data not supported in PyTorch unless p==0
        DIOPI_CHECK(((DiopiDataType::isFloatPoint(input_tensor.dtype()) || p == 0)), "result type Float can't be cast to the desired type");
        std::vector<DiopiTensor*> pTensors{&input_tensor};
        std::set<diopiDtype_t> supportedDtypes{
            diopi_dtype_int8, diopi_dtype_uint8, diopi_dtype_int16, diopi_dtype_int32, diopi_dtype_float16, diopi_dtype_float32};
        autoCastTensorType(ctx, pTensors, supportedDtypes);

        DiopiTensor output_tensor_temp;
        if ((output_tensor.dtype() != input_tensor.dtype())) {
            output_tensor_temp = dataTypeCast(ctx, output_tensor, input_tensor.dtype());
        } else {
            output_tensor_temp = DiopiTensor(out);
        }

        CnnlTensorDesc input_desc(input_tensor, CNNL_LAYOUT_ARRAY);
        CnnlTensorDesc output_desc(output_tensor_temp, CNNL_LAYOUT_ARRAY);
        CnnlTensorDesc mask_desc(mask_tensor, CNNL_LAYOUT_ARRAY);

        // create and set the rand_generator
        cnnlRandGenerator_t generator;
        // MTGP32 algorithm performs better on MLU300 series than MLU200 series
        DIOPI_CALLCNNL(cnnlRandCreateGenerator(&generator, CNNL_RAND_RNG_MTGP32));
        // set the period to the generator
        DIOPI_CALLCNNL(cnnlRandSetMTGP32Period(generator, CNNL_RAND_MTGP32_P11213));
        // create and set the state
        size_t size_state = 0;
        DIOPI_CALLCNNL(cnnlRandGetMTGP32StateSize(generator, &size_state));
        void* state = nullptr;
        state = requiresBuffer(ctx, size_state).data();
        cnnlMTGP32FastParams_t params;
        cnnlRandGetMTGP32HostParam(generator, &params);
        size_t size_kernel = 0;
        cnnlRandGetMTGP32KernelParamSize(generator, &size_kernel);
        void* kernel_params = nullptr;
        kernel_params = requiresBuffer(ctx, size_kernel).data();
        cnnlRandMakeMTGP32Constants(handle, params, kernel_params);
        int rand_seed = time(NULL);
        cnnlRandMakeMTGP32KernelState(handle, state, params, kernel_params, rand_seed);
        DIOPI_CALLCNNL(cnnlFusedDropout_v2(handle,
                                           generator,
                                           input_desc.get(),
                                           input_tensor.data(),
                                           p,
                                           state,
                                           mask_desc.get(),
                                           mask_tensor.data(),
                                           output_desc.get(),
                                           output_tensor_temp.data()));
        DIOPI_CALLCNNL(cnnlRandDestroyGenerator(generator));

        if (output_tensor_temp.dtype() != output_tensor.dtype()) {
            dataTypeCast(ctx, output_tensor, output_tensor_temp);
        }

        return diopiSuccess;
    } else {
        diopiCopyInp(ctx, input, out);
        return diopiSuccess;
    }
}
DIOPI_API diopiError_t diopiDropoutInp(diopiContextHandle_t ctx, diopiTensorHandle_t input, diopiTensorHandle_t mask, double p, bool train) {
    diopiDropout(ctx, input, mask, input, p, train);
    return diopiSuccess;
}

}  // extern "C"
}  // namespace camb
}  // namespace impl