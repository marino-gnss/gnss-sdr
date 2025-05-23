/*!
 * \file tracking_tests_flags.h
 * \brief Helper file for unit testing
 * \author Javier Arribas, 2018. jarribas(at)cttc.es
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

#ifndef GNSS_SDR_TRACKING_TESTS_FLAGS_H
#define GNSS_SDR_TRACKING_TESTS_FLAGS_H

#include <cstdint>
#include <limits>
#include <string>

#if USE_GLOG_AND_GFLAGS
#include <gflags/gflags.h>

DEFINE_string(trk_test_implementation, std::string("GPS_L1_CA_DLL_PLL_Tracking"), "Tracking block implementation under test, defaults to GPS_L1_CA_DLL_PLL_Tracking");
// Input signal configuration
DEFINE_bool(enable_external_signal_file, false, "Use an external signal file capture instead of the software-defined signal generator");
DEFINE_double(external_signal_acquisition_threshold, 2.5, "Threshold for satellite acquisition when external file is used");
DEFINE_int32(external_signal_acquisition_dwells, 5, "Maximum dwells count for satellite acquisition when external file is used");
DEFINE_double(external_signal_acquisition_doppler_max_hz, 5000.0, "Doppler max for satellite acquisition when external file is used");
DEFINE_double(external_signal_acquisition_doppler_step_hz, 125.0, "Doppler step for satellite acquisition when external file is used");
DEFINE_bool(use_acquisition_resampler, false, "Reduce the sampling rate of the input signal for the acquisition in order to optimize the SNR and decrease the processor load");

DEFINE_string(signal_file, std::string("signal_out.bin"), "Path of the external signal capture file");
DEFINE_double(CN0_dBHz_start, std::numeric_limits<double>::infinity(), "Enable noise generator and set the CN0 start sweep value [dB-Hz]");
DEFINE_double(CN0_dBHz_stop, std::numeric_limits<double>::infinity(), "Enable noise generator and set the CN0 stop sweep value [dB-Hz]");
DEFINE_double(CN0_dB_step, 3.0, "Noise generator CN0 sweep step value [dB]");

DEFINE_double(PLL_bw_hz_start, 20.0, "PLL Wide configuration start sweep value [Hz]");
DEFINE_double(PLL_bw_hz_stop, 20.0, "PLL Wide configuration stop sweep value [Hz]");
DEFINE_double(PLL_bw_hz_step, 5.0, "PLL Wide configuration sweep step value [Hz]");

DEFINE_double(DLL_bw_hz_start, 1.0, "DLL Wide configuration start sweep value [Hz]");
DEFINE_double(DLL_bw_hz_stop, 1.0, "DLL Wide configuration stop sweep value [Hz]");
DEFINE_double(DLL_bw_hz_step, 0.25, "DLL Wide configuration sweep step value [Hz]");

DEFINE_double(fll_bw_hz, 4.0, "FLL filter bandwidth [Hz]");
DEFINE_bool(enable_fll_pull_in, false, "Enable FLL in pull-in phase");
DEFINE_bool(enable_fll_steady_state, false, "Enable FLL in steady-state phase");

DEFINE_double(PLL_narrow_bw_hz, 5.0, "PLL Narrow configuration value [Hz]");
DEFINE_double(DLL_narrow_bw_hz, 0.75, "DLL Narrow configuration value [Hz]");

DEFINE_double(acq_Doppler_error_hz_start, 1000.0, "Acquisition Doppler error start sweep value [Hz]");
DEFINE_double(acq_Doppler_error_hz_stop, -1000.0, "Acquisition Doppler error stop sweep value [Hz]");
DEFINE_double(acq_Doppler_error_hz_step, -50.0, "Acquisition Doppler error sweep step value [Hz]");

DEFINE_double(acq_Delay_error_chips_start, 2.0, "Acquisition Code Delay error start sweep value [Chips]");
DEFINE_double(acq_Delay_error_chips_stop, -2.0, "Acquisition Code Delay error stop sweep value [Chips]");
DEFINE_double(acq_Delay_error_chips_step, -0.1, "Acquisition Code Delay error sweep step value [Chips]");

DEFINE_double(acq_to_trk_delay_s, 0.0, "Acquisition to Tracking delay value [s]");


DEFINE_int64(skip_samples, 0, "Skip an initial transitory in the processed signal file capture [samples]");

DEFINE_int32(plot_detail_level, 0, "Specify the desired plot detail (0,1,2): 0 - Minimum plots (default) 2 - Plot all tracking parameters");

DEFINE_double(skip_trk_transitory_s, 1.0, "Skip the initial tracking output signal to avoid transitory results [s]");

// Emulated acquisition configuration

// Tracking configuration
DEFINE_int32(extend_correlation_symbols, 1, "Set the tracking coherent correlation to N symbols (up to 20 for GPS L1 C/A)");
DEFINE_int32(smoother_length, 10, "Set the moving average size for the carrier phase and code phase in case of high dynamics");
DEFINE_bool(high_dyn, false, "Activates the code resampler and NCO generator for high dynamics");

// Test output configuration
DEFINE_bool(plot_gps_l1_tracking_test, false, "Plots results of GpsL1CADllPllTrackingTest with gnuplot");
#else
#include <absl/flags/flag.h>
ABSL_FLAG(std::string, trk_test_implementation, std::string("GPS_L1_CA_DLL_PLL_Tracking"), "Tracking block implementation under test, defaults to GPS_L1_CA_DLL_PLL_Tracking");
// Input signal configuration
ABSL_FLAG(bool, enable_external_signal_file, false, "Use an external signal file capture instead of the software-defined signal generator");
ABSL_FLAG(double, external_signal_acquisition_threshold, 2.5, "Threshold for satellite acquisition when external file is used");
ABSL_FLAG(int32_t, external_signal_acquisition_dwells, 5, "Maximum dwells count for satellite acquisition when external file is used");
ABSL_FLAG(double, external_signal_acquisition_doppler_max_hz, 5000.0, "Doppler max for satellite acquisition when external file is used");
ABSL_FLAG(double, external_signal_acquisition_doppler_step_hz, 125.0, "Doppler step for satellite acquisition when external file is used");
ABSL_FLAG(bool, use_acquisition_resampler, false, "Reduce the sampling rate of the input signal for the acquisition in order to optimize the SNR and decrease the processor load");

ABSL_FLAG(std::string, signal_file, std::string("signal_out.bin"), "Path of the external signal capture file");
ABSL_FLAG(double, CN0_dBHz_start, std::numeric_limits<double>::infinity(), "Enable noise generator and set the CN0 start sweep value [dB-Hz]");
ABSL_FLAG(double, CN0_dBHz_stop, std::numeric_limits<double>::infinity(), "Enable noise generator and set the CN0 stop sweep value [dB-Hz]");
ABSL_FLAG(double, CN0_dB_step, 3.0, "Noise generator CN0 sweep step value [dB]");

ABSL_FLAG(double, PLL_bw_hz_start, 20.0, "PLL Wide configuration start sweep value [Hz]");
ABSL_FLAG(double, PLL_bw_hz_stop, 20.0, "PLL Wide configuration stop sweep value [Hz]");
ABSL_FLAG(double, PLL_bw_hz_step, 5.0, "PLL Wide configuration sweep step value [Hz]");

ABSL_FLAG(double, DLL_bw_hz_start, 1.0, "DLL Wide configuration start sweep value [Hz]");
ABSL_FLAG(double, DLL_bw_hz_stop, 1.0, "DLL Wide configuration stop sweep value [Hz]");
ABSL_FLAG(double, DLL_bw_hz_step, 0.25, "DLL Wide configuration sweep step value [Hz]");

ABSL_FLAG(double, fll_bw_hz, 4.0, "FLL filter bandwidth [Hz]");
ABSL_FLAG(bool, enable_fll_pull_in, false, "Enable FLL in pull-in phase");
ABSL_FLAG(bool, enable_fll_steady_state, false, "Enable FLL in steady-state phase");

ABSL_FLAG(double, PLL_narrow_bw_hz, 5.0, "PLL Narrow configuration value [Hz]");
ABSL_FLAG(double, DLL_narrow_bw_hz, 0.75, "DLL Narrow configuration value [Hz]");

ABSL_FLAG(double, acq_Doppler_error_hz_start, 1000.0, "Acquisition Doppler error start sweep value [Hz]");
ABSL_FLAG(double, acq_Doppler_error_hz_stop, -1000.0, "Acquisition Doppler error stop sweep value [Hz]");
ABSL_FLAG(double, acq_Doppler_error_hz_step, -50.0, "Acquisition Doppler error sweep step value [Hz]");

ABSL_FLAG(double, acq_Delay_error_chips_start, 2.0, "Acquisition Code Delay error start sweep value [Chips]");
ABSL_FLAG(double, acq_Delay_error_chips_stop, -2.0, "Acquisition Code Delay error stop sweep value [Chips]");
ABSL_FLAG(double, acq_Delay_error_chips_step, -0.1, "Acquisition Code Delay error sweep step value [Chips]");

ABSL_FLAG(double, acq_to_trk_delay_s, 0.0, "Acquisition to Tracking delay value [s]");


ABSL_FLAG(int64_t, skip_samples, 0, "Skip an initial transitory in the processed signal file capture [samples]");

ABSL_FLAG(int32_t, plot_detail_level, 0, "Specify the desired plot detail (0,1,2): 0 - Minimum plots (default) 2 - Plot all tracking parameters");

ABSL_FLAG(double, skip_trk_transitory_s, 1.0, "Skip the initial tracking output signal to avoid transitory results [s]");

// Emulated acquisition configuration

// Tracking configuration
ABSL_FLAG(int32_t, extend_correlation_symbols, 1, "Set the tracking coherent correlation to N symbols (up to 20 for GPS L1 C/A)");
ABSL_FLAG(int32_t, smoother_length, 10, "Set the moving average size for the carrier phase and code phase in case of high dynamics");
ABSL_FLAG(bool, high_dyn, false, "Activates the code resampler and NCO generator for high dynamics");

// Test output configuration
ABSL_FLAG(bool, plot_gps_l1_tracking_test, false, "Plots results of GpsL1CADllPllTrackingTest with gnuplot");

#endif
#endif
