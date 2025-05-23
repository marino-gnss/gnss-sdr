/*!
 * \file pcps_opencl_acquisition_cc.cc
 * \brief This class implements a Parallel Code Phase Search Acquisition
 * using OpenCL to offload some functions to the GPU.
 *
 *  Acquisition strategy (Kay Borre book + CFAR threshold).
 *  <ol>
 *  <li> Compute the input signal power estimation
 *  <li> Doppler serial search loop
 *  <li> Perform the FFT-based circular convolution (parallel time search)
 *  <li> Record the maximum peak and the associated synchronization parameters
 *  <li> Compute the test statistics and compare to the threshold
 *  <li> Declare positive or negative acquisition using a message port
 *  </ol>
 *
 * Kay Borre book: K.Borre, D.M.Akos, N.Bertelsen, P.Rinder, and S.H.Jensen,
 * "A Software-Defined GPS and Galileo Receiver. A Single-Frequency
 * Approach", Birkhauser, 2007. pp 81-84
 *
 * \authors <ul>
 *          <li> Javier Arribas, 2011. jarribas(at)cttc.es
 *          <li> Luis Esteve, 2012. luis(at)epsilon-formacion.com
 *          <li> Marc Molina, 2013. marc.molina.pena@gmail.com
 *          </ul>
 *
 * -----------------------------------------------------------------------------
 *
 * GNSS-SDR is a Global Navigation Satellite System software-defined receiver.
 * This file is part of GNSS-SDR.
 *
 * Copyright (C) 2010-2020  (see AUTHORS file for a list of contributors)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * -----------------------------------------------------------------------------
 */

#include "pcps_opencl_acquisition_cc.h"
#include "MATH_CONSTANTS.h"  // TWO_PI
#include "opencl/fft_base_kernels.h"
#include "opencl/fft_internal.h"
#include <gnuradio/io_signature.h>
#include <volk/volk.h>
#include <volk_gnsssdr/volk_gnsssdr.h>
#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#if USE_GLOG_AND_GFLAGS
#include <glog/logging.h>
#else
#include <absl/log/log.h>
#endif


pcps_opencl_acquisition_cc_sptr pcps_make_opencl_acquisition_cc(
    uint32_t sampled_ms, uint32_t max_dwells,
    uint32_t doppler_max, int64_t fs_in,
    int samples_per_ms, int samples_per_code,
    bool bit_transition_flag,
    bool dump,
    const std::string &dump_filename,
    bool enable_monitor_output)
{
    return pcps_opencl_acquisition_cc_sptr(
        new pcps_opencl_acquisition_cc(sampled_ms, max_dwells, doppler_max, fs_in, samples_per_ms,
            samples_per_code, bit_transition_flag, dump, dump_filename, enable_monitor_output));
}


pcps_opencl_acquisition_cc::pcps_opencl_acquisition_cc(
    uint32_t sampled_ms,
    uint32_t max_dwells,
    uint32_t doppler_max,
    int64_t fs_in,
    int samples_per_ms,
    int samples_per_code,
    bool bit_transition_flag,
    bool dump,
    const std::string &dump_filename,
    bool enable_monitor_output)
    : gr::block("pcps_opencl_acquisition_cc",
          gr::io_signature::make(1, 1, static_cast<int>(sizeof(gr_complex) * sampled_ms * samples_per_ms)),
          gr::io_signature::make(0, 1, sizeof(Gnss_Synchro))),
      d_cl_fft_batch_size(1),
      d_dump_filename(dump_filename),
      d_fs_in(fs_in),
      d_sample_counter(0ULL),
      d_mag(0),
      d_input_power(0.0),
      d_samples_per_ms(samples_per_ms),
      d_samples_per_code(samples_per_code),
      d_state(0),
      d_doppler_max(doppler_max),
      d_sampled_ms(sampled_ms),
      d_max_dwells(max_dwells),
      d_well_count(0),
      d_fft_size(d_sampled_ms * d_samples_per_ms),
      d_fft_size_pow2(pow(2, ceil(log2(2 * d_fft_size)))),
      d_num_doppler_bins(0),
      d_in_dwell_count(0),
      d_bit_transition_flag(bit_transition_flag),
      d_active(false),
      d_core_working(false),
      d_dump(dump),
      d_enable_monitor_output(enable_monitor_output)
{
    this->message_port_register_out(pmt::mp("events"));

    d_in_buffer = std::vector<std::vector<gr_complex>>(d_max_dwells, std::vector<gr_complex>(d_fft_size));
    d_magnitude = std::vector<float>(d_fft_size);
    d_fft_codes = std::vector<gr_complex>(d_fft_size_pow2);
    d_zero_vector = std::vector<gr_complex>(d_fft_size_pow2 - d_fft_size, 0.0);

    d_opencl = init_opencl_environment("math_kernel.cl");

    if (d_opencl != 0)
        {
            // Direct FFT
            d_fft_if = gnss_fft_fwd_make_unique(d_fft_size);

            // Inverse FFT
            d_ifft = gnss_fft_rev_make_unique(d_fft_size);
        }
}


pcps_opencl_acquisition_cc::~pcps_opencl_acquisition_cc()
{
    if (d_opencl == 0)
        {
            delete d_cl_queue;
            delete d_cl_buffer_in;
            delete d_cl_buffer_1;
            delete d_cl_buffer_2;
            delete d_cl_buffer_magnitude;
            delete d_cl_buffer_fft_codes;
            if (d_num_doppler_bins > 0)
                {
                    delete[] d_cl_buffer_grid_doppler_wipeoffs;
                }

            clFFT_DestroyPlan(d_cl_fft_plan);
        }

    try
        {
            if (d_dump)
                {
                    d_dump_file.close();
                }
        }
    catch (const std::ofstream::failure &e)
        {
            std::cerr << "Problem closing Acquisition dump file: " << d_dump_filename << '\n';
        }
    catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
}


int pcps_opencl_acquisition_cc::init_opencl_environment(const std::string &kernel_filename)
{
    // get all platforms (drivers)
    std::vector<cl::Platform> all_platforms;
    cl::Platform::get(&all_platforms);

    if (all_platforms.empty())
        {
            std::cout << "No OpenCL platforms found. Check OpenCL installation!\n";
            return 1;
        }

    d_cl_platform = all_platforms[0];  // get default platform
    std::cout << "Using platform: " << d_cl_platform.getInfo<CL_PLATFORM_NAME>()
              << '\n';

    // get default GPU device of the default platform
    std::vector<cl::Device> gpu_devices;
    d_cl_platform.getDevices(CL_DEVICE_TYPE_GPU, &gpu_devices);

    if (gpu_devices.empty())
        {
            std::cout << "No GPU devices found. Check OpenCL installation!\n";
            return 2;
        }

    d_cl_device = gpu_devices[0];

    std::vector<cl::Device> device;
    device.push_back(d_cl_device);
    std::cout << "Using device: " << d_cl_device.getInfo<CL_DEVICE_NAME>() << '\n';

    cl::Context context(device);
    d_cl_context = context;

    // build the program from the source in the file
    std::ifstream kernel_file(kernel_filename, std::ifstream::in);
    std::string kernel_code(std::istreambuf_iterator<char>(kernel_file),
        (std::istreambuf_iterator<char>()));
    kernel_file.close();

    cl::Program::Sources sources;

    sources.push_back({kernel_code.c_str(), kernel_code.length()});

    cl::Program program(context, sources);
    if (program.build(device) != CL_SUCCESS)
        {
            std::cout << " Error building: "
                      << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device[0])
                      << '\n';
            return 3;
        }
    d_cl_program = program;

    // create buffers on the device
    d_cl_buffer_in = new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(gr_complex) * d_fft_size);
    d_cl_buffer_fft_codes = new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(gr_complex) * d_fft_size_pow2);
    d_cl_buffer_1 = new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(gr_complex) * d_fft_size_pow2);
    d_cl_buffer_2 = new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(gr_complex) * d_fft_size_pow2);
    d_cl_buffer_magnitude = new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(float) * d_fft_size);

    // create queue to which we will push commands for the device.
    d_cl_queue = new cl::CommandQueue(d_cl_context, d_cl_device);

    // create FFT plan
    cl_int err;
    clFFT_Dim3 dim = {d_fft_size_pow2, 1, 1};

    d_cl_fft_plan = clFFT_CreatePlan(d_cl_context(), dim, clFFT_1D,
        clFFT_InterleavedComplexFormat, &err);

    if (err != 0)
        {
            delete d_cl_queue;
            delete d_cl_buffer_in;
            delete d_cl_buffer_1;
            delete d_cl_buffer_2;
            delete d_cl_buffer_magnitude;
            delete d_cl_buffer_fft_codes;

            std::cout << "Error creating OpenCL FFT plan.\n";
            return 4;
        }

    return 0;
}


void pcps_opencl_acquisition_cc::init()
{
    d_gnss_synchro->Flag_valid_acquisition = false;
    d_gnss_synchro->Flag_valid_symbol_output = false;
    d_gnss_synchro->Flag_valid_pseudorange = false;
    d_gnss_synchro->Flag_valid_word = false;
    d_gnss_synchro->Acq_doppler_step = 0U;
    d_gnss_synchro->Acq_delay_samples = 0.0;
    d_gnss_synchro->Acq_doppler_hz = 0.0;
    d_gnss_synchro->Acq_samplestamp_samples = 0ULL;
    d_mag = 0.0;
    d_input_power = 0.0;

    // Count the number of bins
    d_num_doppler_bins = 0;
    for (int doppler = static_cast<int>(-d_doppler_max);
        doppler <= static_cast<int>(d_doppler_max);
        doppler += d_doppler_step)
        {
            d_num_doppler_bins++;
        }

    // Create the carrier Doppler wipeoff signals
    d_grid_doppler_wipeoffs = std::vector<std::vector<gr_complex>>(d_num_doppler_bins, std::vector<gr_complex>(d_fft_size));
    if (d_opencl == 0)
        {
            d_cl_buffer_grid_doppler_wipeoffs = new cl::Buffer *[d_num_doppler_bins];
        }

    for (uint32_t doppler_index = 0; doppler_index < d_num_doppler_bins; doppler_index++)
        {
            int doppler = -static_cast<int>(d_doppler_max) + d_doppler_step * doppler_index;
            float phase_step_rad = static_cast<float>(TWO_PI) * doppler / static_cast<float>(d_fs_in);
            std::array<float, 1> _phase{};
            volk_gnsssdr_s32f_sincos_32fc(d_grid_doppler_wipeoffs[doppler_index].data(), -phase_step_rad, _phase.data(), d_fft_size);

            if (d_opencl == 0)
                {
                    d_cl_buffer_grid_doppler_wipeoffs[doppler_index] =
                        new cl::Buffer(d_cl_context, CL_MEM_READ_WRITE, sizeof(gr_complex) * d_fft_size);

                    d_cl_queue->enqueueWriteBuffer(*(d_cl_buffer_grid_doppler_wipeoffs[doppler_index]),
                        CL_TRUE, 0, sizeof(gr_complex) * d_fft_size,
                        d_grid_doppler_wipeoffs[doppler_index].data());
                }
        }

    // zero padding in buffer_1 (FFT input)
    if (d_opencl == 0)
        {
            d_cl_queue->enqueueWriteBuffer(*d_cl_buffer_1, CL_TRUE, sizeof(gr_complex) * d_fft_size,
                sizeof(gr_complex) * (d_fft_size_pow2 - d_fft_size), d_zero_vector.data());
        }
}


void pcps_opencl_acquisition_cc::set_local_code(std::complex<float> *code)
{
    if (d_opencl == 0)
        {
            d_cl_queue->enqueueWriteBuffer(*d_cl_buffer_2, CL_TRUE, 0,
                sizeof(gr_complex) * d_fft_size, code);

            d_cl_queue->enqueueWriteBuffer(*d_cl_buffer_2, CL_TRUE, sizeof(gr_complex) * d_fft_size,
                sizeof(gr_complex) * (d_fft_size_pow2 - 2 * d_fft_size),
                d_zero_vector.data());

            d_cl_queue->enqueueWriteBuffer(*d_cl_buffer_2, CL_TRUE, sizeof(gr_complex) * (d_fft_size_pow2 - d_fft_size),
                sizeof(gr_complex) * d_fft_size, code);

            clFFT_ExecuteInterleaved((*d_cl_queue)(), d_cl_fft_plan, d_cl_fft_batch_size,
                clFFT_Forward, (*d_cl_buffer_2)(), (*d_cl_buffer_2)(),
                0, nullptr, nullptr);

            // Conjucate the local code
            cl::Kernel kernel = cl::Kernel(d_cl_program, "conj_vector");
            kernel.setArg(0, *d_cl_buffer_2);          // input
            kernel.setArg(1, *d_cl_buffer_fft_codes);  // output
            d_cl_queue->enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(d_fft_size_pow2), cl::NullRange);
        }
    else
        {
            std::copy(code, code + d_fft_size, d_fft_if->get_inbuf());

            d_fft_if->execute();  // We need the FFT of local code

            // Conjugate the local code
            volk_32fc_conjugate_32fc(d_fft_codes.data(), d_fft_if->get_outbuf(), d_fft_size);
        }
}


void pcps_opencl_acquisition_cc::acquisition_core_volk()
{
    // initialize acquisition algorithm
    int doppler;
    uint32_t indext = 0;
    float magt = 0.0;
    float fft_normalization_factor = static_cast<float>(d_fft_size) * static_cast<float>(d_fft_size);
    uint64_t samplestamp = d_sample_counter_buffer[d_well_count];

    d_input_power = 0.0;
    d_mag = 0.0;

    d_well_count++;

    DLOG(INFO) << "Channel: " << d_channel
               << " , doing acquisition of satellite: " << d_gnss_synchro->System << " " << d_gnss_synchro->PRN
               << " ,sample stamp: " << d_sample_counter << ", threshold: "
               << d_threshold << ", doppler_max: " << d_doppler_max
               << ", doppler_step: " << d_doppler_step;

    // 1- Compute the input signal power estimation
    volk_32fc_magnitude_squared_32f(d_magnitude.data(), d_in_buffer[d_well_count].data(), d_fft_size);
    volk_32f_accumulator_s32f(&d_input_power, d_magnitude.data(), d_fft_size);
    d_input_power /= static_cast<float>(d_fft_size);

    // 2- Doppler frequency search loop
    for (uint32_t doppler_index = 0; doppler_index < d_num_doppler_bins; doppler_index++)
        {
            // doppler search steps
            doppler = -static_cast<int>(d_doppler_max) + d_doppler_step * doppler_index;

            volk_32fc_x2_multiply_32fc(d_fft_if->get_inbuf(), d_in_buffer[d_well_count].data(),
                d_grid_doppler_wipeoffs[doppler_index].data(), d_fft_size);

            // 3- Perform the FFT-based convolution  (parallel time search)
            // Compute the FFT of the carrier wiped--off incoming signal
            d_fft_if->execute();

            // Multiply carrier wiped--off, Fourier transformed incoming signal
            // with the local FFT'd code reference using SIMD operations with VOLK library
            volk_32fc_x2_multiply_32fc(d_ifft->get_inbuf(),
                d_fft_if->get_outbuf(), d_fft_codes.data(), d_fft_size);

            // compute the inverse FFT
            d_ifft->execute();

            // Search maximum
            volk_32fc_magnitude_squared_32f(d_magnitude.data(), d_ifft->get_outbuf(), d_fft_size);
            volk_gnsssdr_32f_index_max_32u(&indext, d_magnitude.data(), d_fft_size);

            // Normalize the maximum value to correct the scale factor introduced by FFTW
            magt = d_magnitude[indext] / (fft_normalization_factor * fft_normalization_factor);

            // 4- record the maximum peak and the associated synchronization parameters
            if (d_mag < magt)
                {
                    d_mag = magt;

                    // In case that d_bit_transition_flag = true, we compare the potentially
                    // new maximum test statistics (d_mag/d_input_power) with the value in
                    // d_test_statistics. When the second dwell is being processed, the value
                    // of d_mag/d_input_power could be lower than d_test_statistics (i.e,
                    // the maximum test statistics in the previous dwell is greater than
                    // current d_mag/d_input_power). Note that d_test_statistics is not
                    // restarted between consecutive dwells in multidwell operation.
                    if (d_test_statistics < (d_mag / d_input_power) || !d_bit_transition_flag)
                        {
                            d_gnss_synchro->Acq_delay_samples = static_cast<double>(indext % d_samples_per_code);
                            d_gnss_synchro->Acq_doppler_hz = static_cast<double>(doppler);
                            d_gnss_synchro->Acq_samplestamp_samples = samplestamp;
                            d_gnss_synchro->Acq_doppler_step = d_doppler_step;

                            // 5- Compute the test statistics and compare to the threshold
                            // d_test_statistics = 2 * d_fft_size * d_mag / d_input_power;
                            d_test_statistics = d_mag / d_input_power;
                        }
                }

            // Record results to file if required
            if (d_dump)
                {
                    std::stringstream filename;
                    std::streamsize n = 2 * sizeof(float) * (d_fft_size);  // complex file write
                    filename.str("");
                    filename << "./test_statistics_" << d_gnss_synchro->System
                             << "_" << d_gnss_synchro->Signal[0] << d_gnss_synchro->Signal[1] << "_sat_"
                             << d_gnss_synchro->PRN << "_doppler_" << doppler << ".dat";
                    d_dump_file.open(filename.str().c_str(), std::ios::out | std::ios::binary);
                    d_dump_file.write(reinterpret_cast<char *>(d_ifft->get_outbuf()), n);  // write directly |abs(x)|^2 in this Doppler bin?
                    d_dump_file.close();
                }
        }

    if (!d_bit_transition_flag)
        {
            if (d_test_statistics > d_threshold)
                {
                    d_state = 2;  // Positive acquisition
                }
            else if (d_well_count == d_max_dwells)
                {
                    d_state = 3;  // Negative acquisition
                }
        }
    else
        {
            if (d_well_count == d_max_dwells)  // d_max_dwells = 2
                {
                    if (d_test_statistics > d_threshold)
                        {
                            d_state = 2;  // Positive acquisition
                        }
                    else
                        {
                            d_state = 3;  // Negative acquisition
                        }
                }
        }

    d_core_working = false;
}


void pcps_opencl_acquisition_cc::acquisition_core_opencl()
{
    // initialize acquisition algorithm
    int doppler;
    uint32_t indext = 0;
    float magt = 0.0;
    float fft_normalization_factor = (static_cast<float>(d_fft_size_pow2) * static_cast<float>(d_fft_size));  // This works, but I am not sure why.
    uint64_t samplestamp = d_sample_counter_buffer[d_well_count];

    d_input_power = 0.0;
    d_mag = 0.0;

    // write input vector in buffer of OpenCL device
    d_cl_queue->enqueueWriteBuffer(*d_cl_buffer_in, CL_TRUE, 0, sizeof(gr_complex) * d_fft_size, d_in_buffer[d_well_count].data());

    d_well_count++;

    //    struct timeval tv;
    //    long long int begin = 0;
    //    long long int end = 0;

    //    gettimeofday(&tv, NULL);
    //    begin = tv.tv_sec *1e6 + tv.tv_usec;

    DLOG(INFO) << "Channel: " << d_channel
               << " , doing acquisition of satellite: " << d_gnss_synchro->System << " " << d_gnss_synchro->PRN
               << " ,sample stamp: " << d_sample_counter << ", threshold: "
               << d_threshold << ", doppler_max: " << d_doppler_max
               << ", doppler_step: " << d_doppler_step;

    // 1- Compute the input signal power estimation
    volk_32fc_magnitude_squared_32f(d_magnitude.data(), d_in_buffer[d_well_count].data(), d_fft_size);
    volk_32f_accumulator_s32f(&d_input_power, d_magnitude.data(), d_fft_size);
    d_input_power /= static_cast<float>(d_fft_size);

    cl::Kernel kernel;

    // 2- Doppler frequency search loop
    for (uint32_t doppler_index = 0; doppler_index < d_num_doppler_bins; doppler_index++)
        {
            // doppler search steps
            doppler = -static_cast<int>(d_doppler_max) + d_doppler_step * doppler_index;

            // Multiply input signal with doppler wipe-off
            kernel = cl::Kernel(d_cl_program, "mult_vectors");
            kernel.setArg(0, *d_cl_buffer_in);                                    // input 1
            kernel.setArg(1, *d_cl_buffer_grid_doppler_wipeoffs[doppler_index]);  // input 2
            kernel.setArg(2, *d_cl_buffer_1);                                     // output
            d_cl_queue->enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(d_fft_size),
                cl::NullRange);

            // In the previous operation, we store the result in the first d_fft_size positions
            // of d_cl_buffer_1. The rest d_fft_size_pow2-d_fft_size already have zeros
            // (zero-padding is made in init() for optimization purposes).
            clFFT_ExecuteInterleaved((*d_cl_queue)(), d_cl_fft_plan, d_cl_fft_batch_size,
                clFFT_Forward, (*d_cl_buffer_1)(), (*d_cl_buffer_2)(),
                0, nullptr, nullptr);

            // Multiply carrier wiped--off, Fourier transformed incoming signal
            // with the local FFT'd code reference
            kernel = cl::Kernel(d_cl_program, "mult_vectors");
            kernel.setArg(0, *d_cl_buffer_2);          // input 1
            kernel.setArg(1, *d_cl_buffer_fft_codes);  // input 2
            kernel.setArg(2, *d_cl_buffer_2);          // output
            d_cl_queue->enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(d_fft_size_pow2),
                cl::NullRange);

            // compute the inverse FFT
            clFFT_ExecuteInterleaved((*d_cl_queue)(), d_cl_fft_plan, d_cl_fft_batch_size,
                clFFT_Inverse, (*d_cl_buffer_2)(), (*d_cl_buffer_2)(),
                0, nullptr, nullptr);

            // Compute magnitude
            kernel = cl::Kernel(d_cl_program, "magnitude_squared");
            kernel.setArg(0, *d_cl_buffer_2);          // input 1
            kernel.setArg(1, *d_cl_buffer_magnitude);  // output
            d_cl_queue->enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(d_fft_size),
                cl::NullRange);

            // This is the only function that blocks this thread until all previously enqueued
            // OpenCL commands are completed.
            d_cl_queue->enqueueReadBuffer(*d_cl_buffer_magnitude, CL_TRUE, 0,
                sizeof(float) * d_fft_size, d_magnitude.data());

            // Search maximum
            // @TODO: find an efficient way to search the maximum with OpenCL in the GPU.
            volk_gnsssdr_32f_index_max_32u(&indext, d_magnitude.data(), d_fft_size);

            // Normalize the maximum value to correct the scale factor introduced by FFTW
            magt = d_magnitude[indext] / (fft_normalization_factor * fft_normalization_factor);

            // 4- record the maximum peak and the associated synchronization parameters
            if (d_mag < magt)
                {
                    d_mag = magt;

                    // In case that d_bit_transition_flag = true, we compare the potentially
                    // new maximum test statistics (d_mag/d_input_power) with the value in
                    // d_test_statistics. When the second dwell is being processed, the value
                    // of d_mag/d_input_power could be lower than d_test_statistics (i.e,
                    // the maximum test statistics in the previous dwell is greater than
                    // current d_mag/d_input_power). Note that d_test_statistics is not
                    // restarted between consecutive dwells in multidwell operation.
                    if (d_test_statistics < (d_mag / d_input_power) || !d_bit_transition_flag)
                        {
                            d_gnss_synchro->Acq_delay_samples = static_cast<double>(indext % d_samples_per_code);
                            d_gnss_synchro->Acq_doppler_hz = static_cast<double>(doppler);
                            d_gnss_synchro->Acq_samplestamp_samples = samplestamp;
                            d_gnss_synchro->Acq_doppler_step = d_doppler_step;

                            // 5- Compute the test statistics and compare to the threshold
                            // d_test_statistics = 2 * d_fft_size * d_mag / d_input_power;
                            d_test_statistics = d_mag / d_input_power;
                        }
                }

            // Record results to file if required
            if (d_dump)
                {
                    std::stringstream filename;
                    std::streamsize n = 2 * sizeof(float) * (d_fft_size);  // complex file write
                    filename.str("");
                    filename << "./test_statistics_" << d_gnss_synchro->System
                             << "_" << d_gnss_synchro->Signal[0] << d_gnss_synchro->Signal[1] << "_sat_"
                             << d_gnss_synchro->PRN << "_doppler_" << doppler << ".dat";
                    d_dump_file.open(filename.str().c_str(), std::ios::out | std::ios::binary);
                    d_dump_file.write(reinterpret_cast<char *>(d_ifft->get_outbuf()), n);  // write directly |abs(x)|^2 in this Doppler bin?
                    d_dump_file.close();
                }
        }

    //    gettimeofday(&tv, NULL);
    //    end = tv.tv_sec *1e6 + tv.tv_usec;
    //    std::cout << "Acq time = " << (end-begin) << " us\n";

    if (!d_bit_transition_flag)
        {
            if (d_test_statistics > d_threshold)
                {
                    d_state = 2;  // Positive acquisition
                }
            else if (d_well_count == d_max_dwells)
                {
                    d_state = 3;  // Negative acquisition
                }
        }
    else
        {
            if (d_well_count == d_max_dwells)  // d_max_dwells = 2
                {
                    if (d_test_statistics > d_threshold)
                        {
                            d_state = 2;  // Positive acquisition
                        }
                    else
                        {
                            d_state = 3;  // Negative acquisition
                        }
                }
        }

    d_core_working = false;
}


void pcps_opencl_acquisition_cc::set_state(int state)
{
    d_state = state;
    if (d_state == 1)
        {
            d_gnss_synchro->Acq_delay_samples = 0.0;
            d_gnss_synchro->Acq_doppler_hz = 0.0;
            d_gnss_synchro->Acq_samplestamp_samples = 0ULL;
            d_gnss_synchro->Acq_doppler_step = 0U;
            d_well_count = 0;
            d_mag = 0.0;
            d_input_power = 0.0;
            d_test_statistics = 0.0;
            d_in_dwell_count = 0;
            d_sample_counter_buffer.clear();
        }
    else if (d_state == 0)
        {
        }
    else
        {
            LOG(ERROR) << "State can only be set to 0 or 1";
        }
}


int pcps_opencl_acquisition_cc::general_work(int noutput_items,
    gr_vector_int &ninput_items, gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items)
{
    int acquisition_message = -1;  // 0=STOP_CHANNEL 1=ACQ_SUCCEES 2=ACQ_FAIL
    switch (d_state)
        {
        case 0:
            {
                if (d_active)
                    {
                        // restart acquisition variables
                        d_gnss_synchro->Acq_delay_samples = 0.0;
                        d_gnss_synchro->Acq_doppler_hz = 0.0;
                        d_gnss_synchro->Acq_samplestamp_samples = 0ULL;
                        d_gnss_synchro->Acq_doppler_step = 0U;
                        d_well_count = 0;
                        d_mag = 0.0;
                        d_input_power = 0.0;
                        d_test_statistics = 0.0;
                        d_in_dwell_count = 0;
                        d_sample_counter_buffer.clear();

                        d_state = 1;
                    }

                d_sample_counter += static_cast<uint64_t>(d_fft_size) * ninput_items[0];  // sample counter

                break;
            }

        case 1:
            {
                if (d_in_dwell_count < d_max_dwells)
                    {
                        // Fill internal buffer with d_max_dwells signal blocks. This step ensures that
                        // consecutive signal blocks will be processed in multi-dwell operation. This is
                        // essential when d_bit_transition_flag = true.
                        uint32_t num_dwells = std::min(static_cast<int>(d_max_dwells - d_in_dwell_count), ninput_items[0]);
                        for (uint32_t i = 0; i < num_dwells; i++)
                            {
                                const auto *in = reinterpret_cast<const gr_complex *>(input_items[i]);
                                std::copy(in, in + d_fft_size, d_in_buffer[d_in_dwell_count++].data());
                                d_sample_counter += static_cast<uint64_t>(d_fft_size);
                                d_sample_counter_buffer.push_back(d_sample_counter);
                            }

                        if (ninput_items[0] > static_cast<int>(num_dwells))
                            {
                                d_sample_counter += static_cast<uint64_t>(d_fft_size * (ninput_items[0] - num_dwells));
                            }
                    }
                else
                    {
                        // We already have d_max_dwells consecutive blocks in the internal buffer,
                        // just skip input blocks.
                        d_sample_counter += static_cast<uint64_t>(d_fft_size) * ninput_items[0];
                    }

                // We create a new thread to process next block if the following
                // conditions are fulfilled:
                //   1. There are new blocks in d_in_buffer that have not been processed yet
                //      (d_well_count < d_in_dwell_count).
                //   2. No other acquisition_core thead is working (!d_core_working).
                //   3. d_state==1. We need to check again d_state because it can be modified at any
                //      moment by the external thread (may have changed since checked in the switch()).
                //      If the external thread has already declared positive (d_state=2) or negative
                //      (d_state=3) acquisition, we don't have to process next block!!
                if ((d_well_count < d_in_dwell_count) && !d_core_working && d_state == 1)
                    {
                        d_core_working = true;
                        if (d_opencl == 0)
                            {  // Use OpenCL implementation
                                boost::thread(&pcps_opencl_acquisition_cc::acquisition_core_opencl, this);
                            }
                        else
                            {  // Use Volk implementation
                                boost::thread(&pcps_opencl_acquisition_cc::acquisition_core_volk, this);
                            }
                    }

                break;
            }

        case 2:
            {
                // Declare positive acquisition using a message port
                DLOG(INFO) << "positive acquisition";
                DLOG(INFO) << "satellite " << d_gnss_synchro->System << " " << d_gnss_synchro->PRN;
                DLOG(INFO) << "sample_stamp " << d_sample_counter;
                DLOG(INFO) << "test statistics value " << d_test_statistics;
                DLOG(INFO) << "test statistics threshold " << d_threshold;
                DLOG(INFO) << "code phase " << d_gnss_synchro->Acq_delay_samples;
                DLOG(INFO) << "doppler " << d_gnss_synchro->Acq_doppler_hz;
                DLOG(INFO) << "magnitude " << d_mag;
                DLOG(INFO) << "input signal power " << d_input_power;

                d_active = false;
                d_state = 0;

                d_sample_counter += static_cast<uint64_t>(d_fft_size) * ninput_items[0];  // sample counter

                acquisition_message = 1;
                this->message_port_pub(pmt::mp("events"), pmt::from_long(acquisition_message));

                // Copy and push current Gnss_Synchro to monitor queue
                if (d_enable_monitor_output)
                    {
                        auto **out = reinterpret_cast<Gnss_Synchro **>(&output_items[0]);
                        Gnss_Synchro current_synchro_data = Gnss_Synchro();
                        current_synchro_data = *d_gnss_synchro;
                        *out[0] = std::move(current_synchro_data);
                        noutput_items = 1;  // Number of Gnss_Synchro objects produced
                    }

                break;
            }

        case 3:
            {
                // Declare negative acquisition using a message port
                DLOG(INFO) << "negative acquisition";
                DLOG(INFO) << "satellite " << d_gnss_synchro->System << " " << d_gnss_synchro->PRN;
                DLOG(INFO) << "sample_stamp " << d_sample_counter;
                DLOG(INFO) << "test statistics value " << d_test_statistics;
                DLOG(INFO) << "test statistics threshold " << d_threshold;
                DLOG(INFO) << "code phase " << d_gnss_synchro->Acq_delay_samples;
                DLOG(INFO) << "doppler " << d_gnss_synchro->Acq_doppler_hz;
                DLOG(INFO) << "magnitude " << d_mag;
                DLOG(INFO) << "input signal power " << d_input_power;

                d_active = false;
                d_state = 0;

                d_sample_counter += static_cast<uint64_t>(d_fft_size) * ninput_items[0];  // sample counter

                acquisition_message = 2;
                this->message_port_pub(pmt::mp("events"), pmt::from_long(acquisition_message));

                break;
            }
        }

    consume_each(ninput_items[0]);

    return noutput_items;
}
