#ifndef RecoPixelVertexing_PixelVertexFinding_src_gpuFitVertices_h
#define RecoPixelVertexing_PixelVertexFinding_src_gpuFitVertices_h

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "CUDACore/HistoContainer.h"
#include "CUDACore/cuda_assert.h"

#include "gpuVertexFinder.h"

namespace gpuVertexFinder {

  __device__ __forceinline__ void fitVertices(ZVertices* pdata,
                                              WorkSpace* pws,
                                              float chi2Max  // for outlier rejection
  ) {
    constexpr bool verbose = false;  // in principle the compiler should optmize out if false

    auto& __restrict__ data = *pdata;
    auto& __restrict__ ws = *pws;
    auto nt = ws.ntrks;
    float const* __restrict__ zt = ws.zt;
    float const* __restrict__ ezt2 = ws.ezt2;
    float* __restrict__ zv = data.zv;
    float* __restrict__ wv = data.wv;
    float* __restrict__ chi2 = data.chi2;
    uint32_t& nvFinal = data.nvFinal;
    uint32_t& nvIntermediate = ws.nvIntermediate;

    int32_t* __restrict__ nn = data.ndof;
    int32_t* __restrict__ iv = ws.iv;

    assert(pdata);
    assert(zt);

    assert(nvFinal <= nvIntermediate);
    nvFinal = nvIntermediate;
    auto foundClusters = nvFinal;

    // zero
    for (auto i = threadIdx.x; i < foundClusters; i += blockDim.x) {
      zv[i] = 0;
      wv[i] = 0;
      chi2[i] = 0;
    }

    // only for test
    __shared__ int noise;
    if (verbose && 0 == threadIdx.x)
      noise = 0;

    __syncthreads();
    std::atomic_ref<int> noise_atomic{noise};
    // compute cluster location
    for (auto i = threadIdx.x; i < nt; i += blockDim.x) {
      if (iv[i] > 9990) {
        if (verbose)
          ++noise_atomic;
        continue;
      }
      assert(iv[i] >= 0);
      assert(iv[i] < int(foundClusters));
      auto w = 1.f / ezt2[i];
      std::atomic_ref<float> zv_atomic{zv[iv[i]]};
      zv_atomic.fetch_add(zt[i] * w);
      std::atomic_ref<float> wv_atomic{wv[iv[i]]};
      wv_atomic.fetch_add(w);
    }

    __syncthreads();
    // reuse nn
    for (auto i = threadIdx.x; i < foundClusters; i += blockDim.x) {
      assert(wv[i] > 0.f);
      zv[i] /= wv[i];
      nn[i] = -1;  // ndof
    }
    __syncthreads();

    // compute chi2
    for (auto i = threadIdx.x; i < nt; i += blockDim.x) {
      if (iv[i] > 9990)
        continue;

      auto c2 = zv[iv[i]] - zt[i];
      c2 *= c2 / ezt2[i];
      if (c2 > chi2Max) {
        iv[i] = 9999;
        continue;
      }
      std::atomic_ref<float> chi2_atomic{chi2[iv[i]]};
      chi2_atomic.fetch_add(c2);
      std::atomic_ref<int32_t> nn_atomic{nn[iv[i]]};
      ++nn_atomic;
    }
    __syncthreads();
    for (auto i = threadIdx.x; i < foundClusters; i += blockDim.x)
      if (nn[i] > 0)
        wv[i] *= float(nn[i]) / chi2[i];

    if (verbose && 0 == threadIdx.x)
      printf("found %d proto clusters ", foundClusters);
    if (verbose && 0 == threadIdx.x)
      printf("and %d noise\n", noise);
  }

  __global__ void fitVerticesKernel(ZVertices* pdata,
                                    WorkSpace* pws,
                                    float chi2Max  // for outlier rejection
  ) {
    fitVertices(pdata, pws, chi2Max);
  }

}  // namespace gpuVertexFinder

#endif  // RecoPixelVertexing_PixelVertexFinding_src_gpuFitVertices_h
