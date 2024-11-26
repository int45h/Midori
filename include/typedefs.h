#pragma once

#include <stdio.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef size_t usize;

#define LOG_ERROR(err, ...) fprintf(stderr, "error: " err, ##__VA_ARGS__)
#define MIN_VAL(a,b) (((a)<(b))?(a):(b))
#define MAX_VAL(a,b) (((a)>(b))?(a):(b))

#define VERTEX_SIZE 8

typedef enum
{
    MD_SUCCESS,
    MD_ERROR_UNKNOWN,
    MD_ERROR_FILE_NOT_FOUND,
    MD_ERROR_FILE_READ_FAILURE,
    MD_ERROR_MEMORY_ALLOCATION_FAILURE,
    MD_ERROR_OBJ_LOADING_FAILURE,
    MD_ERROR_WINDOW_FAILURE,
    MD_ERROR_VULKAN_INSTANCE_FAILURE,
    MD_ERROR_VULKAN_PHYSICAL_DEVICE_FAILURE,
    MD_ERROR_VULKAN_LOGICAL_DEVICE_FAILURE,
    MD_ERROR_VULKAN_QUEUE_NOT_PRESENT,
    MD_ERROR_VULKAN_SWAPCHAIN_FAILURE,
    MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_FAILURE,
    MD_ERROR_VULKAN_SWAPCHAIN_IMAGE_VIEW_FAILURE
}
MdResult;

#define MD_CHECK(result, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return result;}
#define MD_CHECK_VOID(result, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return;}
#define MD_CHECK_ANY(result, ret, err, ...) if (result != MD_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return ret;}

#define VK_CHECK(result, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return result;}
#define VK_CHECK_VOID(result, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return;}
#define VK_CHECK_ANY(result, ret, err, ...) if (result != VK_SUCCESS) {LOG_ERROR(err, ##__VA_ARGS__); return ret;}