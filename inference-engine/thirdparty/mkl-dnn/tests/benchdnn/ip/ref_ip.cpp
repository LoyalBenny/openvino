/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "src/common/mkldnn_thread.hpp"

#include "ip/ip.hpp"

namespace ip {

void compute_ref_fwd(const prb_t *p, dnn_mem_t &src_m,
        dnn_mem_t &wei_m, dnn_mem_t &bia_m, dnn_mem_t &dst_m) {

    int M = p->mb;
    int N = p->oc;
    int K = p->ic * p->id * p->ih * p->iw;

    gemm("C", "N", "T", M, N, K, 1.f, (float *)src_m, K, (float *)wei_m, K,
        0.f, (float *)dst_m, N);

    mkldnn::impl::parallel_nd(p->mb, p->oc, [&](int mb, int oc) {
        size_t dst_off = dst_off_f(p, mb, oc);
        float &d = ((float *)dst_m)[dst_off];
        if (p->dir & FLAG_BIA) {
            size_t bia_off = bia_off_f(p, oc);
            d += ((float *)bia_m)[bia_off];
        }
        maybe_scale(d, p->scales, oc, p->attr);
        maybe_post_ops(d, 0, p->attr);
    });
}

void compute_ref_bwd_d(const prb_t *p, dnn_mem_t &diff_src_m,
        dnn_mem_t &wei_m, dnn_mem_t &diff_dst_m) {

    int M = p->mb;
    int N = p->ic * p->id * p->ih * p->iw;
    int K = p->oc;

    gemm("C", "N", "N", M, N, K, 1.f, (float *)diff_dst_m, K, (float *)wei_m, N,
        0.f, (float *)diff_src_m, N);
}

void compute_ref_bwd_w(const prb_t *p, dnn_mem_t &src_m,
        dnn_mem_t &diff_wei_m, dnn_mem_t &diff_bia_m, dnn_mem_t &diff_dst_m) {

    int M = p->oc;
    int N = p->ic * p->id * p->ih * p->iw;
    int K = p->mb;

    gemm("C", "T", "N", M, N, K, 1.f, (float *)diff_dst_m, M, (float *)src_m, N,
        0.f, (float *)diff_wei_m, N);

    if (!(p->dir & FLAG_BIA)) return;

    mkldnn::impl::parallel_nd(p->oc, [&](int oc) {
        size_t bia_off = bia_off_f(p, oc);
        float &db = ((float *)diff_bia_m)[bia_off];
        db = 0;
        for (int mb = 0; mb < p->mb; ++mb) {
            size_t dst_off = dst_off_f(p, mb, oc);
            db += ((float *)diff_dst_m)[dst_off];
        }
    });
}

}
