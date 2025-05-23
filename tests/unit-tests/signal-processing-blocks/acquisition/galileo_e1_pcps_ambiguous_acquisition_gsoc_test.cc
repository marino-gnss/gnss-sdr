/*!
 * \file galileo_e1_pcps_ambiguous_acquisition_gsoc_test.cc
 * \brief  This class implements an acquisition test for
 *  GalileoE1PcpsAmbiguousAcquisition class based on GSoC 2012 experiments.
 *
 * This test is a part of an experiment performed by Luis Esteve in the
 * framework of the Google Summer of Code (GSoC) 2012, with the collaboration
 * of Javier Arribas and Carles Fernández, related to the extension of GNSS-SDR
 *  to Galileo. The objective is perform a positive acquisition of in-orbit
 *  Galileo signals in the E1 band.
 *
 *  Report:
 *  https://docs.google.com/document/d/1SZ3m1K7Qf9GsZQGEF7VSOEewBDCjbylCClw9rSXwG7Y/edit?pli=1
 *
 * \author Luis Esteve, 2012. luis(at)epsilon-formacion.com
 *
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


#include "concurrent_queue.h"
#include "galileo_e1_pcps_ambiguous_acquisition.h"
#include "gnss_block_factory.h"
#include "gnss_block_interface.h"
#include "gnss_sdr_valve.h"
#include "gnss_signal.h"
#include "gnss_synchro.h"
#include "in_memory_configuration.h"
#include <boost/exception/diagnostic_information.hpp>
#include <gnuradio/analog/sig_source_waveform.h>
#include <gnuradio/blocks/file_source.h>
#include <gnuradio/blocks/null_sink.h>
#include <gnuradio/blocks/skiphead.h>
#include <gnuradio/top_block.h>
#include <pmt/pmt.h>
#include <chrono>
#include <utility>
#if HAS_GENERIC_LAMBDA
#else
#include <boost/bind/bind.hpp>
#endif
#ifdef GR_GREATER_38
#include <gnuradio/analog/sig_source.h>
#else
#include <gnuradio/analog/sig_source_c.h>
#endif
#if PMT_USES_BOOST_ANY
namespace wht = boost;
#else
namespace wht = std;
#endif

// ######## GNURADIO BLOCK MESSAGE RECEIVER #########
class GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx;

using GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_sptr = gnss_shared_ptr<GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx>;

GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_sptr GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_make(Concurrent_Queue<int>& queue);


class GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx : public gr::block
{
private:
    friend GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_sptr GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_make(Concurrent_Queue<int>& queue);
    void msg_handler_channel_events(const pmt::pmt_t msg);
    explicit GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx(Concurrent_Queue<int>& queue);
    Concurrent_Queue<int>& channel_internal_queue;

public:
    int rx_message;
    ~GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx();  //!< Default destructor
};


GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_sptr GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_make(Concurrent_Queue<int>& queue)
{
    return GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_sptr(new GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx(queue));
}


void GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx::msg_handler_channel_events(const pmt::pmt_t msg)
{
    try
        {
            int64_t message = pmt::to_long(std::move(msg));
            rx_message = message;
            channel_internal_queue.push(rx_message);
        }
    catch (const wht::bad_any_cast& e)
        {
            LOG(WARNING) << "msg_handler_channel_events Bad any_cast: " << e.what();
            rx_message = 0;
        }
}


GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx::GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx(Concurrent_Queue<int>& queue) : gr::block("GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx", gr::io_signature::make(0, 0, 0), gr::io_signature::make(0, 0, 0)), channel_internal_queue(queue)
{
    this->message_port_register_in(pmt::mp("events"));
    this->set_msg_handler(pmt::mp("events"),
#if HAS_GENERIC_LAMBDA
        [this](auto&& PH1) { msg_handler_channel_events(PH1); });
#else
#if USE_BOOST_BIND_PLACEHOLDERS
        boost::bind(&GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx::msg_handler_channel_events, this, boost::placeholders::_1));
#else
        boost::bind(&GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx::msg_handler_channel_events, this, _1));
#endif
#endif
    rx_message = 0;
}

GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx::~GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx() = default;


// ###########################################################


class GalileoE1PcpsAmbiguousAcquisitionGSoCTest : public ::testing::Test
{
protected:
    GalileoE1PcpsAmbiguousAcquisitionGSoCTest()
    {
        factory = std::make_shared<GNSSBlockFactory>();
        config = std::make_shared<InMemoryConfiguration>();
        item_size = sizeof(gr_complex);
        stop = false;
        message = 0;
        gnss_synchro = Gnss_Synchro();
    }

    ~GalileoE1PcpsAmbiguousAcquisitionGSoCTest() = default;

    void init();
    void start_queue();
    void wait_message();
    void stop_queue();

    Concurrent_Queue<int> channel_internal_queue;
    std::shared_ptr<Concurrent_Queue<pmt::pmt_t>> queue;
    gr::top_block_sptr top_block;
    std::shared_ptr<GNSSBlockFactory> factory;
    std::shared_ptr<InMemoryConfiguration> config;
    Gnss_Synchro gnss_synchro;
    size_t item_size;
    bool stop;
    int message;
    std::thread ch_thread;
};


void GalileoE1PcpsAmbiguousAcquisitionGSoCTest::init()
{
    gnss_synchro.Channel_ID = 0;
    gnss_synchro.System = 'E';
    std::string signal = "1C";
    signal.copy(gnss_synchro.Signal, 2, 0);
    gnss_synchro.PRN = 11;

    config->set_property("Acquisition_1B.implementation", "Galileo_E1_PCPS_Ambiguous_Acquisition");
    config->set_property("GNSS-SDR.internal_fs_sps", "4000000");
    config->set_property("Acquisition_1B.item_type", "gr_complex");
    config->set_property("Acquisition_1B.coherent_integration_time_ms", "4");
    config->set_property("Acquisition_1B.dump", "false");
    // config->set_property("Acquisition_1B.threshold", "2.5");
    config->set_property("Acquisition_1B.pfa", "0.001");
    config->set_property("Acquisition_1B.doppler_max", "10000");
    config->set_property("Acquisition_1B.doppler_step", "125");
    config->set_property("Acquisition_1B.repeat_satellite", "false");
    config->set_property("Acquisition_1B.cboc", "true");
}


void GalileoE1PcpsAmbiguousAcquisitionGSoCTest::start_queue()
{
    ch_thread = std::thread(&GalileoE1PcpsAmbiguousAcquisitionGSoCTest::wait_message, this);
}


void GalileoE1PcpsAmbiguousAcquisitionGSoCTest::wait_message()
{
    while (!stop)
        {
            try
                {
                    channel_internal_queue.wait_and_pop(message);
                    stop_queue();
                }
            catch (const boost::exception& e)
                {
                    DLOG(WARNING) << "Boost exception: " << boost::diagnostic_information(e);
                }
        }
}


void GalileoE1PcpsAmbiguousAcquisitionGSoCTest::stop_queue()
{
    stop = true;
}


TEST_F(GalileoE1PcpsAmbiguousAcquisitionGSoCTest, Instantiate)
{
    init();
    std::shared_ptr<GNSSBlockInterface> acq_ = factory->GetBlock(config.get(), "Acquisition_1B", 1, 0);
    std::shared_ptr<AcquisitionInterface> acquisition = std::dynamic_pointer_cast<AcquisitionInterface>(acq_);
    EXPECT_STREQ("Galileo_E1_PCPS_Ambiguous_Acquisition", acquisition->implementation().c_str());
}


TEST_F(GalileoE1PcpsAmbiguousAcquisitionGSoCTest, ConnectAndRun)
{
    int fs_in = 4000000;
    int nsamples = 4 * fs_in;
    std::chrono::time_point<std::chrono::system_clock> start, end;
    std::chrono::duration<double> elapsed_seconds(0);
    queue = std::make_shared<Concurrent_Queue<pmt::pmt_t>>();
    top_block = gr::make_top_block("Acquisition test");

    init();
    std::shared_ptr<GNSSBlockInterface> acq_ = factory->GetBlock(config.get(), "Acquisition_1B", 1, 0);
    std::shared_ptr<AcquisitionInterface> acquisition = std::dynamic_pointer_cast<AcquisitionInterface>(acq_);
    auto msg_rx = GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_make(channel_internal_queue);

    ASSERT_NO_THROW({
        acquisition->connect(top_block);
        auto source = gr::analog::sig_source_c::make(fs_in, gr::analog::GR_SIN_WAVE, 1000, 1, gr_complex(0));
        auto valve = gnss_sdr_make_valve(sizeof(gr_complex), nsamples, queue.get());
        top_block->connect(source, 0, valve, 0);
        top_block->connect(valve, 0, acquisition->get_left_block(), 0);
        top_block->msg_connect(acquisition->get_right_block(), pmt::mp("events"), msg_rx, pmt::mp("events"));
    }) << "Failure connecting the blocks of acquisition test.";

    EXPECT_NO_THROW({
        start = std::chrono::system_clock::now();
        top_block->run();  // Start threads and wait
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;
    }) << "Failure running the top_block.";
    std::cout << "Processed " << nsamples << " samples in " << elapsed_seconds.count() * 1e6 << " microseconds\n";
}


TEST_F(GalileoE1PcpsAmbiguousAcquisitionGSoCTest, ValidationOfResults)
{
    std::chrono::time_point<std::chrono::system_clock> start, end;
    std::chrono::duration<double> elapsed_seconds(0);
    queue = std::make_shared<Concurrent_Queue<pmt::pmt_t>>();
    top_block = gr::make_top_block("Acquisition test");

    init();
    std::shared_ptr<GNSSBlockInterface> acq_ = factory->GetBlock(config.get(), "Acquisition_1B", 1, 0);
    std::shared_ptr<GalileoE1PcpsAmbiguousAcquisition> acquisition = std::dynamic_pointer_cast<GalileoE1PcpsAmbiguousAcquisition>(acq_);
    auto msg_rx = GalileoE1PcpsAmbiguousAcquisitionGSoCTest_msg_rx_make(channel_internal_queue);

    ASSERT_NO_THROW({
        acquisition->set_channel(gnss_synchro.Channel_ID);
    }) << "Failure setting channel.";

    ASSERT_NO_THROW({
        acquisition->set_gnss_synchro(&gnss_synchro);
    }) << "Failure setting gnss_synchro.";

    ASSERT_NO_THROW({
        acquisition->set_threshold(config->property("Acquisition_1B.threshold", 0.00001));
    }) << "Failure setting threshold.";

    ASSERT_NO_THROW({
        acquisition->set_doppler_max(config->property("Acquisition_1B.doppler_max", 10000));
    }) << "Failure setting doppler_max.";

    ASSERT_NO_THROW({
        acquisition->set_doppler_step(config->property("Acquisition_1B.doppler_step", 250));
    }) << "Failure setting doppler_step.";

    ASSERT_NO_THROW({
        acquisition->connect(top_block);
    }) << "Failure connecting acquisition to the top_block.";

    ASSERT_NO_THROW({
        std::string path = std::string(TEST_PATH);
        // std::string file = path + "signal_samples/GSoC_CTTC_capture_2012_07_26_4Msps_4ms.dat";
        std::string file = path + "signal_samples/Galileo_E1_ID_1_Fs_4Msps_8ms.dat";
        const char* file_name = file.c_str();
        gr::blocks::file_source::sptr file_source = gr::blocks::file_source::make(sizeof(gr_complex), file_name, false);
        top_block->connect(file_source, 0, acquisition->get_left_block(), 0);
        top_block->msg_connect(acquisition->get_right_block(), pmt::mp("events"), msg_rx, pmt::mp("events"));
    }) << "Failure connecting the blocks of acquisition test.";

    ASSERT_NO_THROW({
        start_queue();
        acquisition->set_local_code();
        acquisition->init();
        acquisition->reset();
        acquisition->set_state(1);
    }) << "Failure starting acquisition";

    EXPECT_NO_THROW({
        start = std::chrono::system_clock::now();
        top_block->run();  // Start threads and wait
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;
    }) << "Failure running the top_block.";

    stop_queue();

    uint64_t nsamples = gnss_synchro.Acq_samplestamp_samples;
    std::cout << "Acquired " << nsamples << " samples in " << elapsed_seconds.count() * 1e6 << " microseconds\n";

    EXPECT_EQ(2, message) << "Acquisition failure. Expected message: 0=ACQ STOP.";

    ch_thread.join();
}
