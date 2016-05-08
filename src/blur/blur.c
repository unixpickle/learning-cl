#include "blur.h"
#include "math.h"

int blur_image(bmp_t * image, int radius, cl_float sigma) {
  cl_float * weights = make_weights(radius);
  if (!weights) {
    return -1;
  }

  cl_uchar4 * output = malloc(sizeof(cl_uchar4) * image->width * image->height);
  if (!output) {
    free(weights);
    return -1;
  }

  size_t bitmapSize = image->width * image->height * sizeof(cl_uchar4);
  context * ctx = context_create(bitmapSize, image->pixels, output, weights, radius);
  if (!ctx) {
    free(weights);
    free(output);
    return -1;
  }

  if (context_run(ctx)) {
    context_destroy(ctx);
    free(weights);
    free(outputs);
    return -1;
  }

  memcpy(image->pixels, output, bitmapSize);

  context_destroy(ctx);
  free(output);
  free(weights);
  return 0;
}

cl_float * make_weights(int radius) {
  size_t weightsSide = radius*2 + 1;
  size_t weightCount = weightsSide * weightsSide;
  cl_float * weights = (cl_float *)malloc(weightCount * sizeof(cl_float));
  if (!weights) {
    return NULL;
  }
  cl_float weightSum = 0;
  int weightIdx = 0;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      cl_float dist = (cl_float)(x*x + y*y);
      weights[weightIdx] = (cl_float)expf((float)dist / (2 * sigma * sigma));
      weightSum += weights[weightIdx];
      ++weightIdx;
    }
  }
  cl_float weightNorm = 1 / weightSum;
  while (weightIdx--) {
    weightSum[weightIdx] *= weightNorm;
  }
  return weights;
}

static const char * blurKernel = "\
__kernel blur(__global uchar4 * input, __global uchar4 * output, \
              __global float * weights, int radius) { \
  // TODO: apply the blur defined by weights to the input. \
} \
";

typedef struct {
  cl_platform_id platform;
  cl_device_id device;
  cl_context context;
  cl_command_queue queue;
  cl_mem inputBuffer;
  cl_mem outputBuffer;
  cl_mem weightBuffer;
  cl_program program;
  cl_kernel kernel;
} context;

static context * context_create(size_t bitmapSize, void * input, void * output,
                                void * weights, cl_int radius) {
  cl_uint resultCount;
  cl_int statusCode;

  cl_platform_id platform;
  if (clGetPlatformIDs(1, &platform, &resultCount) || resultCount != 1) {
    return NULL;
  }

  cl_device_id device;
  if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &resultCount)) {
    return NULL;
  } else if (resultCount == 0) {
    return NULL;
  }

  context * ctx = (context *)malloc(sizeof(context));
  ctx->platform = platform;
  ctx->device = device;

  ctx->context = clCreateContext(0, 1, &device, NULL, NULL, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  ctx->queue = clCreateCommandQueue(ctx->context, device, 0, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  ctx->inputBuffer = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR,
    bitmapSize, input, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  ctx->outputBuffer = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE|CL_MEM_USE_HOST_PTR,
    bitmapSize, output, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  size_t weightsSide = radius*2 + 1;
  size_t weightCount = weightsSide * weightsSide;
  ctx->weightBuffer = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR,
    weightCount*sizeof(cl_float), weights, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  size_t kernelLen = strlen(blurKernel);
  ctx->program = clCreateProgramWithSource(ctx->context, 1, &blurKernel,
    &kernelLen, &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  if (clBuildProgram(ctx->program, 1, &device, NULL, NULL, NULL)) {
    context_destroy(ctx);
    return NULL;
  }

  ctx->kernel = clCreateKernel(ctx->program, "blur", &statusCode);
  if (statusCode) {
    context_destroy(ctx);
    return NULL;
  }

  void * args[3] = {&ctx->inputBuffer, &ctx->outputBuffer, &ctx->weightBuffer};
  for (int i = 0; i < 3; i++) {
    if (clSetKernelArg(ctx->kernel, i, sizeof(ctx->inputBuffer), args[i])) {
      context_destroy(ctx);
      return NULL;
    }
  }

  if (clSetKernelArg(ctx->kernel, 3, sizeof(radius), &radius)) {
    context_destroy(ctx);
    return NULL;
  }

  return ctx;
}

static int context_run(context * ctx, size_t radius, size_t width, size_t height) {
  cl_event event;
  size_t workSizes[2] = {width - radius*2, height - radius*2};
  size_t workOffsets[2] = {radius, radius};
  if (clEnqueueNDRangeKernel(ctx->queue, ctx->kernel, 2, workOffsets, workSizes,
      NULL, 0, NULL, &event)) {
    return -1;
  }

  cl_int res = clWaitForEvents(1, &event);
  clReleaseEvent(event);
  if (res) {
    return -1;
  } else {
    return 0;
  }
}

static void context_destroy(context * ctx) {
  if (ctx->context) {
    clFlush(ctx->context);
    clFinish(ctx->context);
  }
  if (ctx->program) {
    clReleaseProgram(ctx->program);
  }
  if (ctx->inputBuffer) {
    clReleaseMemObject(ctx->inputBuffer);
  }
  if (ctx->outputBuffer) {
    clReleaseMemObject(ctx->outputBuffer);
  }
  if (ctx->weightBuffer) {
    clReleaseMemObject(ctx->weightBuffer);
  }
  if (ctx->queue) {
    clReleaseCommandQueue(ctx->queue);
  }
  if (ctx->context) {
    clReleaseContext(ctx->context);
  }
  if (ctx->kernel) {
    clReleaseKernel(ctx->kernel);
  }
  free(ctx);
}