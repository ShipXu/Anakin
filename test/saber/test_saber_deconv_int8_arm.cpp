#include "saber/funcs/deconv.h"
#include "saber/funcs/type_trans.h"
#include "saber/funcs/timer.h"
#include "test/saber/test_saber_func.h"
#include "saber/core/tensor_op.h"

using namespace anakin::saber;

#ifdef USE_ARM_PLACE

int g_cluster = 0;
int g_threads = 1;
int g_test_iter = 10;

bool g_basic_test = false;
bool g_compare_result = true;
bool g_flag_relu = false;
bool g_flag_bias = false;

int g_num = 1;
int g_chin = 32;
int g_h_in = 112;
int g_w_in = 112;

int g_ch_out = 32;
int g_group = 32;
int g_kw = 3;
int g_pad_w = 1;
int g_stride_w = 1;
int g_dila_w = 1;
int g_kh = 3;
int g_pad_h = 1;
int g_stride_h = 1;
int g_dila_h = 1;

typedef Tensor<ARM> TensorH;

template <typename dtype>
static int count_diff(const dtype* src1, const dtype* src2, int size, double max_ratio, float tensor_scale) {
    double sum_abs1 = 0.0;
    double sum_abs2 = 0.0;
    for (int i = 0; i < size; ++i) {
        sum_abs1 += fabs(src1[i]);
        sum_abs2 += fabs(src2[i]);
    }
    double mean_abs1 = sum_abs1 / size;
    double mean_abs2 = sum_abs2 / size;
    double mean_val = (mean_abs1 + mean_abs2) / 2.0;
    if (max_ratio <= 0) {
        max_ratio = 0.1;
    }
    int count = 0;
    for (int i = 0; i < size; ++i) {
        double abs_diff = fabs(src1[i] - src2[i]);
        double ratio =  abs_diff / (fabs(src1[i] + src2[i]) + 1e-12);
        if (ratio > max_ratio && abs_diff > (tensor_scale + 1e-5f) && abs_diff > mean_val * 0.1f) {
            ++count;
        }
    }
    return count;
}

template  <typename type,  typename type2>
static void basic_gemm(int m, int n, int k, const type* a, const type* b, const type2* bias, type2* c, \
    type2 alpha, type2 beta, \
    bool trans_a = false, bool trans_b = false, bool flag_bias = false, bool flag_relu = false) {
#pragma omp parallel for
    for (int i = 0; i < m; ++i) {
        type2 bias_data = (type2)0;
        if (flag_bias) {
            bias_data = bias[i];
        }
        for (int j = 0; j < n; ++j) {
            type2 sum = static_cast<type2>(0);
            for (int l = 0; l < k; ++l) {
                type av;
                type bv;
                if (trans_a) {
                    av = a[l * m + i];
                } else{
                    av = a[i * k + l];
                }
                if (trans_b) {
                    bv = b[j * k + l];
                } else {
                    bv = b[l * n + j];
                }
                sum += av * bv;
            }
            type2 tmp = alpha * sum + beta * c[i * n + j] + bias_data;
            if (flag_relu) {
                c[i * n + j] = tmp > (type2)0? tmp : (type2)0;
            } else {
                c[i * n + j] = tmp;
            }
        }
    }
}

inline bool is_a_ge_zero_and_a_lt_b(int a, int b) {
    return static_cast<unsigned>(a) < static_cast<unsigned>(b);
}

template <typename Dtype>
static void col2im(const Dtype* data_col, const int channels,
            const int height, const int width, const int kernel_h, const int kernel_w,
            const int pad_h, const int pad_w,
            const int stride_h, const int stride_w,
            const int dilation_h, const int dilation_w,
            Dtype* data_im) {

    memset(data_im, 0, height * width * channels * sizeof(Dtype));
    const int output_h = (height + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h + 1;
    const int output_w = (width + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w + 1;
    const int channel_size = height * width;

    for (int channel = channels; channel--; data_im += channel_size) {
        for (int kernel_row = 0; kernel_row < kernel_h; kernel_row++) {
            for (int kernel_col = 0; kernel_col < kernel_w; kernel_col++) {
                int input_row = -pad_h + kernel_row * dilation_h;

                for (int output_rows = output_h; output_rows; output_rows--) {
                    if (!is_a_ge_zero_and_a_lt_b(input_row, height)) {
                        data_col += output_w;
                    } else {
                        int input_col = -pad_w + kernel_col * dilation_w;

                        for (int output_col = output_w; output_col; output_col--) {
                            if (is_a_ge_zero_and_a_lt_b(input_col, width)) {
                                data_im[input_row * width + input_col] += *data_col;
                            }
                            data_col++;
                            input_col += stride_w;
                        }
                    }
                    input_row += stride_h;
                }
            }
        }
    }
}

template <typename Dtype>
static void fill_bias_relu(Dtype* tensor, const Dtype* bias, int channel, int channel_size, \
    bool flag_bias, bool flag_relu) {
    Dtype* data = tensor;
    for (int j = 0; j < channel; ++j) {
        Dtype bias_c = flag_bias? bias[j] : 0;
        for (int i = 0; i < channel_size; i++) {
            data[i] += bias_c;
            if (flag_relu) {
                data[i] = data[i] > 0 ? data[i] : 0.f;
            }
        }
        data += channel_size;
    }
}

//! for float, dtype1 and type2 is float
//! for int8, dytpe1 is char, dtype2 is int
template <typename Dtype1, typename Dtype2>
static void deconv_basic(const Dtype1* din, Dtype2* dout, \
                          int num, int chout, int hout, int wout, \
                          int chin, int hin, int win, \
                          const Dtype1* weights, const Dtype2* bias, \
                          int group, int kernel_w, int kernel_h, int stride_w, \
                          int stride_h, int dila_w, int dila_h, \
                          int pad_w, int pad_h, bool flag_bias, bool flag_relu) {


    int m = chout * kernel_w * kernel_h / group;
    int n = hin * win;
    int k = chin / group;

    if (chin != chout || group != chin) {
        CHECK_EQ(chin % group, 0) << "input channel or group size error";
        CHECK_EQ(chout % group, 0) << "output channel or group size error";
    }

    Tensor<ARM> workspace_tensor;
    Shape workspace_shape({1, 1, 1, group * m * n});
    workspace_tensor.re_alloc(workspace_shape, anakin::saber::AK_FLOAT);

    int group_size_in = win * hin * chin / group;
    int group_size_out = wout * hout * chout / group;
    int group_size_coldata = m * n;
    int group_size_weights = chin * chout * kernel_w * kernel_h / (group * group);
    bool flag_1x1s1p1 = (kernel_w == 1) && (kernel_h == 1) && (stride_h == 1) && \
                        (stride_w == 1) && (pad_w == 1) && (pad_h == 1) && \
                        (dila_w == 1) && (dila_h == 1);

    Dtype2* workspace_ptr = static_cast<Dtype2*>(workspace_tensor.mutable_data());

    for (int i = 0; i < num; ++i) {
        const Dtype1* din_batch = din + i * chin * hin * win;
        Dtype2* dout_batch = dout + i * chout * hout * wout;

        Dtype2* col_data = workspace_ptr;
        if (flag_1x1s1p1) {
            col_data = dout_batch;
        }
        memset(col_data, 0, sizeof(Dtype2) * group_size_coldata);
        for (int g = 0; g < group; ++g) {
            const Dtype1* din_group = din_batch + g * group_size_in;
            const Dtype1* weights_group = weights + g * group_size_weights;
            Dtype2* coldata_group = col_data + g * group_size_coldata;
            basic_gemm<Dtype1, Dtype2>(m, n, k, weights_group, din_group, nullptr, coldata_group, \
                (Dtype2)1, (Dtype2)0, true, false, false, (!flag_bias && flag_relu));
        }

        if (!flag_1x1s1p1) {
            col2im(col_data, chout, hout, wout, kernel_h, kernel_w, pad_h, pad_w, \
                stride_h, stride_w, dila_h, dila_w, dout_batch);
        }
        //! add bias
        if (flag_bias) {
            fill_bias_relu(dout_batch, bias, chout, wout * hout, flag_bias, flag_relu);
        }
    }
}

SaberStatus test_arm_deconv_int8(int n, int c, int h, int w, \
    int ch_out, int kernel_w, int kernel_h, int stride_w, int stride_h, int pad_w, int pad_h, \
    int dila_w, int dila_h, int group, bool is_bias, bool is_relu, int thread_num, int cluster_id) {

    double to = 0;
    double min_time = 1000000;
    SaberTimer<ARM> t1;

    Context<ARM> ctx1;
    PowerMode mode = static_cast<PowerMode>(cluster_id);
    ctx1.set_run_mode(mode, thread_num);
    LOG(INFO) << "test threads activated";
#pragma omp parallel
    {
#ifdef USE_OPENMP
        int thread = omp_get_num_threads();
        LOG(INFO) << "number of threads: " << thread;
#endif
    }

    TensorH tout_basic_int32;
    TensorH tout_basic_int8;
    TensorH tout_saber_int32;
    TensorH tout_saber_int8;
    TensorH tout_basic_fp32;
    TensorH tout_saber_fp32;

    TensorH thinf;
    TensorH thinc;
    Shape shin ({n, c, h, w});
    thinf.re_alloc(shin, AK_FLOAT);
    thinc.re_alloc(shin, AK_INT8);

    std::vector<TensorH*> tvin_fp32;
    std::vector<TensorH*> tvin_int8;
    std::vector<TensorH*> tvout_saber_fp32;
    std::vector<TensorH*> tvout_saber_int32;
    std::vector<TensorH*> tvout_saber_int8;

    tvin_fp32.push_back(&thinf);
    tvin_int8.push_back(&thinc);
    tvout_saber_fp32.push_back(&tout_saber_fp32);
    tvout_saber_int32.push_back(&tout_saber_int32);
    tvout_saber_int8.push_back(&tout_saber_int8);

    int num = n;
    int chin = c;
    int hin = h;
    int win = w;

    LOG(INFO) << "conv param: ";
    LOG(INFO) << " img_num = " << num << " in_channels = " << chin << " img_h = " << hin << " img_w = " << win;
    LOG(INFO) << " num_out = " << ch_out << " group = " << group << " kernel_w = " << kernel_w << " kernel_h = " << kernel_h << \
        " stride_width = " << stride_w << " stride_height = " << stride_h << \
         " pad_width = " << pad_w << " pad_height = " << pad_h << \
         " dilation_w = " << dila_w << " dilation_h = " << dila_h;
    LOG(INFO) << " bias flag = " << (is_bias? "true" : "false") << ", relu flag = " << (is_relu? "true" : "false");

    int kernel_extent_h = dila_h * (kernel_h - 1) + 1;
    int hout = (h - 1) * stride_h + kernel_extent_h - 2 * pad_h;
    int kernel_extent_w = dila_w * (kernel_w - 1) + 1;
    int wout = (w - 1) * stride_w + kernel_extent_w - 2 * pad_w;

    Shape shape_out({num, ch_out, hout, wout});

    Shape shw({ch_out, chin / group, kernel_h, kernel_w});
    Shape shb({1, ch_out, 1, 1});

    TensorH pweihtf;
    TensorH pbiasf;

    TensorH pweihtc;
    TensorH pbiasi;

    if (is_bias) {
        pbiasf.re_alloc(shb, AK_FLOAT);
        pbiasi.re_alloc(shb, AK_INT32);
        fill_tensor_rand(pbiasf, -10, 10);
    }

    pweihtf.re_alloc(shw, AK_FLOAT);
    pweihtc.re_alloc(shw, AK_FLOAT);

    fill_tensor_rand(thinf, -20, 20);
    fill_tensor_rand(pweihtf, -10, 10);
    // LOG(INFO) << "thinf:";
    // print_tensor(thinf);
//    fill_tensor_const(thinf, 1.f);
//    fill_tensor_const(pweihtf, 1.f);
//    fill_tensor_const(pbiasf, 1.f);

    pweihtc.copy_from(pweihtf);

    //! convert input data type
    std::vector<float> scale;
    std::vector<float> weights_scale(ch_out, 1.f);
    get_tensor_scale(thinf, scale, 0, 63.f);
    thinf.set_scale(scale);
//    LOG(INFO) << "input tesnor scale at factor 63.f is " << thinf.get_scale()[0] << ", max_val: " << 63.f * thinf.get_scale()[0];
    trans_tensor_dtype<ARM, AK_FLOAT, AK_INT8>(thinf, thinc, scale[0], 1.f, {1.f});
    thinc.set_scale(scale);
    // LOG(INFO) << "thinc:";
    // print_tensor(thinc);
    trans_weights_dtype(pweihtc, AK_INT8, 127.f, DECONV_TYPE, group);
    std::vector<float> w_scale = pweihtc.get_scale();
    trans_fp32_bias_to_int32(pbiasf, pbiasi, thinc.get_scale()[0], w_scale);
//    print_tensor(pweihtc);
//    print_tensor(pbiasi);

    //! get int8 and fp32 basic result
    if (g_compare_result) {
        LOG(INFO) << "run basic conv for precision comparation";
        const char* dinc = static_cast<const char*>(thinc.data());
        const char* weightc = static_cast<const char*>(pweihtc.data());
        const int* biasi = static_cast<const int*>(pbiasi.data());
        const float* dinf = static_cast<const float*>(thinf.data());
        const float* weightf = static_cast<const float*>(pweihtf.data());
        const float* biasf = static_cast<const float*>(pbiasf.data());
        tout_basic_fp32.re_alloc(shape_out, AK_FLOAT);
        tout_basic_int32.re_alloc(shape_out, AK_INT32);
        tout_basic_int8.re_alloc(shape_out, AK_INT8);

        float* dout_basic_fp32 = static_cast<float*>(tout_basic_fp32.mutable_data());
        int* dout_basic_int32 = static_cast<int*>(tout_basic_int32.mutable_data());

//        LOG(INFO) << "do basic fp32 conv";
        deconv_basic<float, float>(dinf, dout_basic_fp32, num, ch_out, hout, wout, chin, hin, win, \
            weightf, biasf, group, kernel_w, kernel_h, stride_w, stride_h, \
            dila_w, dila_h, pad_w, pad_h, is_bias, is_relu);

//        LOG(INFO) << "do basic int8 conv, trans basic int32 to fp32";
//        deconv_basic<char, int>(dinc, dout_basic_int32, num, ch_out, hout, wout, chin, hin, win, \
            weightc, biasi, group, kernel_w, kernel_h, stride_w, stride_h, \
            dila_w, dila_h, pad_w, pad_h, is_bias, is_relu);

//        LOG(INFO) << "trans basic int32 to int8";
//        trans_tensor_int32_to_int8(tout_basic_int32, tout_basic_int8, thinf.get_scale()[0], w_scale, &ctx1);

//        trans_tensor_int32_to_fp32(tout_basic_int32, tout_basic_fp32, thinf.get_scale()[0], w_scale, &ctx1);

//        print_tensor(tout_basic_fp32);
//        print_tensor(tout_basic_int32);
    }

    Deconv<ARM, AK_INT8> deconv_int8;

    ConvParam<ARM> param(group, pad_h, pad_w, stride_h, stride_w, dila_h, dila_w, &pweihtf, &pbiasf);
    if (is_relu){
        ActivationParam<ARM> act_param(Active_relu);
        param.activation_param = act_param;
    }

//    deconv_int8.compute_output_shape(tvin_int8, tvout_saber_int32);
//    Shape sh_out_saber = tvout_saber_int32[0]->valid_shape();
    deconv_int8.compute_output_shape(tvin_int8, tvout_saber_fp32, param);
    Shape sh_out_saber = tvout_saber_fp32[0]->valid_shape();


    LOG(INFO) << "output shape: " << shape_out[0] << ", " << shape_out[1] << ", " \
        << shape_out[2] << ", " << shape_out[3];
    CHECK_EQ(shape_out == sh_out_saber, true) << "compute output shape error";

    //! re_alloc mem for output tensor
//    LOG(INFO) << "re-alloc output memory";
    tvout_saber_int32[0]->re_alloc(shape_out, AK_INT32);
    tvout_saber_fp32[0]->re_alloc(shape_out, AK_FLOAT);
    tvout_saber_int8[0]->re_alloc(shape_out, AK_INT8);

    //! init the op
//    LOG(INFO) << "saber conv impl init";
//    states = deconv_int8.init(tvin_int8, tvout_saber_int32, ctx1);
    auto states = deconv_int8.init(tvin_int8, tvout_saber_fp32, param, SPECIFY, SABER_IMPL, ctx1);
    CHECK_EQ(states, SaberSuccess) << "Saber conv init failed";

    //! compute
//    LOG(INFO) << "saber conv compute";
    to = 0;
    for (int i = 0; i < g_test_iter; ++i) {
        t1.clear();
        t1.start(ctx1);
//        states = deconv_int8.dispatch(tvin_int8, tvout_saber_int32);
        states = deconv_int8(tvin_int8, tvout_saber_fp32, param, ctx1);
        t1.end(ctx1);
        to += t1.get_average_ms();
        if (t1.get_average_ms() < min_time) {
            min_time = t1.get_average_ms();
        }
        CHECK_EQ(states, SaberSuccess) << "Saber conv compute failed";
    }
    long long gops = n * ch_out * wout * ch_out * (chin / group) * kernel_w * kernel_h;
    LOG(INFO) << "saber conv running time, ave: " << to / g_test_iter << ", min time: " << min_time << \
        ", GOPS: " << 0.000001 * gops / min_time;

//    print_tensor(tout_saber_fp32);

    if (g_compare_result) {

        double max_ratio = 0;
        double max_diff = 0;
        tensor_cmp_host((const float*)tout_basic_fp32.data(), (const float*)tout_saber_fp32.data(), \
            tout_basic_fp32.valid_size(), max_ratio, max_diff);
        LOG(INFO) << "compare result, max diff: " << max_diff << ", max ratio: " << max_ratio;
        double mean_basic = tensor_mean_value<ARM>(tout_basic_fp32, nullptr);
        double mean_saber = tensor_mean_value<ARM>(tout_saber_fp32, nullptr);
        LOG(INFO) << "mean_basic: " << mean_basic << ", mean_saber: " << mean_saber;
        double max_ratio_thresh = 2e-1f;
        long long diff_num = count_diff<float>(static_cast<const float*>(tout_basic_fp32.data()), \
            static_cast<const float*>(tout_saber_fp32.data()), tout_saber_fp32.valid_size(), max_ratio_thresh, thinf.get_scale()[0]);
                LOG(INFO) << "number of diff ratio > " << max_ratio_thresh << " is: " << diff_num << ", %" \
            << 100.f * diff_num / tout_basic_fp32.valid_size();
//        double mean_diff_ratio = fabs(mean_basic - mean_saber) / (fabs(mean_basic) + fabs(mean_saber));
//        LOG(INFO) << "mean val diff ratio: " << mean_diff_ratio;
        if ((float)diff_num / tout_saber_fp32.valid_size() > 0.05/* || mean_diff_ratio > 0.1*/) {
            LOG(INFO) << "basic result:";
            print_tensor(tout_basic_fp32);
            LOG(INFO) << "saber result:";
            print_tensor(tout_saber_fp32);
            return SaberInvalidValue;
        }
    }
    return SaberSuccess;
}

#if 1
TEST(TestSaberFunc, test_func_deconv_gemm_int8) {
    if (g_basic_test) {
    for (auto& batch : {1, 2}) {
    for (auto& c : {1, 3, 8, 16}) {
    for (auto& cout : {1, 5, 16}) {
    for (auto& g_div : {1, 2}) {
    for (auto& h : {10, 28, 56, 112, 128, 150, 224, 300}) {
    for (auto& kw : {1, 2, 3, 5}) {
    for (auto& kh : {1, 2, 3, 5}) {
    for (auto& pad : {1, 2}) {
    for (auto& stride : {1, 2}) {
    for (auto& dila : {1, 2}) {
    for (auto &flag_bias : {false, true}) {
    for (auto &flag_relu : {false, true}) {
    for (auto &th : {1/*, 2, 4*/}) {
        int w = h;
        int g = g_div;
        if ((c % g_div != 0) || (cout % g_div != 0)) {
            g = 1;
        }
        auto flag = test_arm_deconv_int8(batch, c, h, w, cout, 1, 1, 1, 1, \
            0, 0, 1, 1, g, flag_bias, flag_relu, th, g_cluster);
        if (flag == SaberSuccess) {
            LOG(INFO) << "test int8 deconv: batchsize: " << batch << ", channel: "
                << c << ", h & w: " << h << ", num_out: " << cout << ", group: " << g << \
                ", bias: " << (flag_bias ? "true" : "false") << ", relu: "
                << (flag_relu ? "true" : "false") << ", threads: " << \
                th << ", cluster: " << g_cluster << " passed!!\n";
        } else {
            LOG(FATAL) << "test int8 deconv: batchsize: " << batch << ", channel: "
                << c << ", h & w: " << h << ", num_out: " << cout << ", group: " << g << \
                ", bias: " << (flag_bias ? "true" : "false") << ", relu: "
                << (flag_relu ? "true" : "false") << ", threads: " << \
                th << ", cluster: " << g_cluster << " failed!!\n";
        }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
}
#endif

#if 1
TEST(TestSaberFunc, test_deconv_int8_costom_size) {
    auto flag = test_arm_deconv_int8(g_num, g_chin, g_h_in, g_w_in, g_ch_out, g_kw, g_kh, g_stride_w, g_stride_h, \
            g_pad_w, g_pad_h, g_dila_w, g_dila_h, g_group, g_flag_bias, g_flag_relu, g_threads, g_cluster);
    if (flag == SaberSuccess) {
        LOG(INFO) << "test int8 deconv: batchsize: " << g_num << ", channel: "
                << g_chin << ", h & w: " << g_h_in << ", num_out: " << g_ch_out << ", group: " << g_group << \
                ", bias: " << (g_flag_bias ? "true" : "false") << ", relu: "
                << (g_flag_relu ? "true" : "false") << ", threads: " << \
                g_threads << ", cluster: " << g_cluster << " passed!!\n";
    } else {
        LOG(INFO) << "test int8 deconv: batchsize: " << g_num << ", channel: "
                          << g_chin << ", h & w: " << g_h_in << ", num_out: " << g_ch_out << ", group: " << g_group << \
                ", bias: " << (g_flag_bias ? "true" : "false") << ", relu: "
                          << (g_flag_relu ? "true" : "false") << ", threads: " << \
                g_threads << ", cluster: " << g_cluster << " failed!!\n";
    }
}
#endif

int main(int argc, const char** argv){
    Env<ARM>::env_init();
            LOG(ERROR) << "usage: ./" << argv[0] << " basic_test cluster  threads  test_iter " << \
                " compare_result flag_bias flag_relu num ch_in h_in w_in ch_out group" << \
                " kernel pad stride dila [kernel_h] [pad_h] [stride_h] [dila_h]";

    if (argc >= 2) {
        g_basic_test = atoi(argv[1]) > 0;
    }

    if (argc >= 3) {
        g_cluster = atoi(argv[2]);
    }
    if (argc >= 4) {
        g_threads = atoi(argv[3]);
    }
    if (argc >= 5) {
        g_test_iter = atoi(argv[4]);
    }
    if (argc >= 6) {
        g_compare_result = atoi(argv[5]) > 0;
    }
    if (argc >= 7) {
        g_flag_bias = atoi(argv[6]) > 0;
    }
    if (argc >= 8) {
        g_flag_relu = atoi(argv[7]) > 0;
    }
    if (argc >= 9) {
        if (argc < 18) {
            LOG(FATAL) << "usage: ./" << argv[0] << " basic_test cluster  threads  test_iter " << \
                " compare_result flag_bias flag_relu num ch_in h_in w_in ch_out group" << \
                " kernel pad stride dila [kernel_h] [pad_h] [stride_h] [dila_h]";
            return -1;
        }
        g_num = atoi(argv[8]);
        g_chin = atoi(argv[9]);
        g_h_in = atoi(argv[10]);
        g_w_in = atoi(argv[11]);
        g_ch_out = atoi(argv[12]);
        g_group = atoi(argv[13]);
        g_kw = atoi(argv[14]);
        g_kh = g_kw;
        g_pad_w = atoi(argv[15]);
        g_pad_h = g_pad_w;
        g_stride_w = atoi(argv[16]);
        g_stride_h = g_stride_w;
        g_dila_w = atoi(argv[17]);
        g_dila_h = g_dila_w;
    }
    if (argc > 18) {
        g_kh = atoi(argv[18]);
    }
    if (argc > 19) {
        g_pad_h = atoi(argv[19]);
    }
    if (argc > 20) {
        g_stride_h = atoi(argv[20]);
    }
    if (argc > 21) {
        g_dila_h = atoi(argv[21]);
    }

    // initial logger
    logger::init(argv[0]);
    InitTest();
    RUN_ALL_TESTS(argv[0]);
    return 0;
}

#else

int main(int argc, const char** argv){
    LOG(INFO) << "this unit test only be used in TargetType is ARM";
    return 0;
}

#endif

