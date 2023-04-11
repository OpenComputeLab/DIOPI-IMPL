/**
 * @file
 * @author DeepLink
 * @copyright  (c) 2023, DeepLink.
 */

#include <cnrt.h>

#include <memory>
#include <set>

#include "common.hpp"

namespace impl {
namespace camb {

#define _MAKE_KEY(a, b) (((static_cast<uint64_t>(a) & 0xFFFFFFFF) << 32) | (static_cast<uint64_t>(b) & 0xFFFFFFFF))

// special convert (cnnl doesn't support)
constexpr uint64_t BoolInt64 = _MAKE_KEY(diopi_dtype_bool, diopi_dtype_int64);
constexpr uint64_t Int16Int64 = _MAKE_KEY(diopi_dtype_int16, diopi_dtype_int64);
constexpr uint64_t Uint8Bool = _MAKE_KEY(diopi_dtype_uint8, diopi_dtype_bool);
constexpr uint64_t Int16Bool = _MAKE_KEY(diopi_dtype_int16, diopi_dtype_bool);
constexpr uint64_t Int64Bool = _MAKE_KEY(diopi_dtype_int64, diopi_dtype_bool);
constexpr uint64_t Int8Bool = _MAKE_KEY(diopi_dtype_int8, diopi_dtype_bool);
constexpr uint64_t Int8Int64 = _MAKE_KEY(diopi_dtype_int8, diopi_dtype_int64);
constexpr uint64_t Int64Int8 = _MAKE_KEY(diopi_dtype_int64, diopi_dtype_int8);

// Cast through middle type
#define CAST_TYPE_THROUGH_INT32(TYPE)                   \
    {                                                   \
        dataTypeCast(ctx, src, diopi_dtype_int32); \
        cast_type = TYPE;                               \
        break;                                          \
    }

diopiError_t dataTypeCastInternal(diopiContextHandle_t ctx, DiopiTensor src, DiopiTensor dest) {
    cnnlHandle_t handle = cnnlHandlePool.get(ctx);
    diopiDtype_t srcDtype = src.dtype();
    diopiDtype_t destDtype = dest.dtype();
    cnnlCastDataType_t cast_type;

    if (gCnnlCastDataTypeMapping.find({srcDtype, destDtype}) != gCnnlCastDataTypeMapping.end()) {
        // directly cast
        cast_type = gCnnlCastDataTypeMapping.at({srcDtype, destDtype});
    } else {
        // cast through middle
        auto key = _MAKE_KEY(srcDtype, destDtype);
        switch (key) {
            case BoolInt64:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_INT64)
            case Int16Int64:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_INT64)
            case Int8Int64:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_INT64)
            case Uint8Bool:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_BOOL)
            case Int16Bool:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_BOOL)
            case Int64Bool:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_BOOL)
            case Int8Bool:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_BOOL)
            case Int64Int8:
                CAST_TYPE_THROUGH_INT32(CNNL_CAST_INT32_TO_INT8)

            default:
                // TODO(waiting for dispatch) : cast through cpu
                set_last_error_string("Can not cast from %d to %d at %s:%d ", srcDtype, destDtype, __FILE__, __LINE__);
                return diopiDtypeNotSupported;
        }
    }

    CnnlTensorDesc input_desc(src, CNNL_LAYOUT_ARRAY);
    CnnlTensorDesc output_desc(dest, CNNL_LAYOUT_ARRAY);

    DIOPI_CHECKCNNL(cnnlCastDataType(handle, input_desc.get(), src.data(), cast_type, output_desc.get(), dest.data()));

    return diopiSuccess;
}

diopiError_t dataTypeCast(diopiContextHandle_t& ctx, DiopiTensor& src, diopiDtype_t destDtype) {
    if (src.dtype() == destDtype) {
        return diopiSuccess;
    }
    DiopiTensor dest = requiresTensor(ctx, src.shape(), destDtype);
    DIOPI_CALL(dataTypeCastInternal(ctx, src, dest));
    src = dest;
    return diopiSuccess;
}

diopiError_t dataTypeCast(diopiContextHandle_t ctx, DiopiTensor& dest, const DiopiTensor& src) {
    if (src.dtype() == dest.dtype()) {
        return diopiSuccess;
    }
    // check size of dest and src
    DIOPI_CHECK(src.shape() == dest.shape(), "the shapes of src and dest are not equal");

    return dataTypeCastInternal(ctx, src, dest);
}

static diopiError_t choiceDtype(const std::set<diopiDtype_t>& opSupportedDtypes, diopiDtype_t* dtype) {
    if (opSupportedDtypes.find(diopi_dtype_float32) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_float32;
    } else if (opSupportedDtypes.find(diopi_dtype_float16) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_float16;
    } else if (opSupportedDtypes.find(diopi_dtype_int32) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_int32;
    } else if (opSupportedDtypes.find(diopi_dtype_int16) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_int16;
    } else if (opSupportedDtypes.find(diopi_dtype_int8) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_int8;
    } else if (opSupportedDtypes.find(diopi_dtype_bool) != opSupportedDtypes.end()) {
        *dtype = diopi_dtype_bool;
    } else {
        set_last_error_string("this operator does not support bool, int8, int16, int32, float16, float32");
        return diopiDtypeNotSupported;
    }
    return diopiSuccess;
}

diopiError_t autoCastTensorType(diopiContextHandle_t ctx, const std::vector<DiopiTensor*>& pTensors, const std::set<diopiDtype_t>& opSupportedDtype) {
    // std::multimap<diopiDtype_t, DiopiTensor*> dtypeAndTensorPtrs;
    std::set<diopiDtype_t> dtypeAndTensorPtrs;
    diopiDtype_t targetType = diopi_dtype_float32;
    for (const auto& pTensor : pTensors) {
        dtypeAndTensorPtrs.insert(pTensor->dtype());
    }
    if (dtypeAndTensorPtrs.find(diopi_dtype_float64) != dtypeAndTensorPtrs.end() || dtypeAndTensorPtrs.find(diopi_dtype_float32) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_float32) == opSupportedDtype.end()) {  // not support float32
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into float32
            targetType = diopi_dtype_float32;
        }
    } else if (dtypeAndTensorPtrs.find(diopi_dtype_float16) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_float16) == opSupportedDtype.end()) {  // not support float16
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into float16
            targetType = diopi_dtype_float16;
        }
    } else if (dtypeAndTensorPtrs.find(diopi_dtype_int64) != dtypeAndTensorPtrs.end() ||
               dtypeAndTensorPtrs.find(diopi_dtype_int32) != dtypeAndTensorPtrs.end() ||
               dtypeAndTensorPtrs.find(diopi_dtype_uint64) != dtypeAndTensorPtrs.end() ||
               dtypeAndTensorPtrs.find(diopi_dtype_uint32) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_int32) == opSupportedDtype.end()) {  // not support int32
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into int32
            targetType = diopi_dtype_int32;
        }
    } else if (dtypeAndTensorPtrs.find(diopi_dtype_int16) != dtypeAndTensorPtrs.end() ||
               dtypeAndTensorPtrs.find(diopi_dtype_uint16) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_int16) == opSupportedDtype.end()) {  // not support int16
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into int16
            targetType = diopi_dtype_int16;
        }
    } else if (dtypeAndTensorPtrs.find(diopi_dtype_int8) != dtypeAndTensorPtrs.end() ||
               dtypeAndTensorPtrs.find(diopi_dtype_uint8) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_int8) == opSupportedDtype.end()) {  // not support int8
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into int8
            targetType = diopi_dtype_int8;
        }
    } else if (dtypeAndTensorPtrs.find(diopi_dtype_bool) != dtypeAndTensorPtrs.end()) {
        if (opSupportedDtype.find(diopi_dtype_bool) == opSupportedDtype.end()) {  // not support bool
            DIOPI_CALL(choiceDtype(opSupportedDtype, &targetType));
        } else {  // all tensors cast into bool
            targetType = diopi_dtype_bool;
        }
    } else {
        set_last_error_string("tensor's dtype error, can't be cast");
        return diopiDtypeNotSupported;
    }
    for (const auto& pTensor : pTensors) {
        DIOPI_CALL(dataTypeCast(ctx, *pTensor, targetType));
    }
    return diopiSuccess;
}

}  // namespace camb
}  // namespace impl
