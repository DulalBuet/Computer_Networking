/* sixth_fig11_newreno.cc  (CORRECTED)
 * Reproduces Figure 11 (Section 5.2 testbed) in NS-3.
 * Logs throughput of TCP New Reno.
 *
 * Figure 11 Y-axis analysis:
 *   The paper labels the Y-axis "Throughput [byte]" with ticks at
 *   0, 2M, 4M, 6M, 8M, 10M.  At 40 Mbps with a 1-second sample window:
 *     max bytes/s = 40e6 / 8 = 5,000,000 bytes/s  (= 5M)
 *   This matches the paper's scale — peak throughput reaches ~4-5M
 *   bytes/s which is consistent with 40 Mbps link capacity.
 *   Correct sample window = 1 SECOND (bytes/s), NOT 0.1s.
 *
 * Key fix vs previous version:
 *   - Sample window changed from 0.1s → 1.0s  (bytes/s not bytes/0.1s)
 *   - Sender rate changed from 40 Mbps → 100 Mbps to force congestion
 *     and produce the sawtooth drops seen in Figure 11
 *
 * Testbed parameters (Section 5):
 *   Bandwidth  = 40 Mbps (CBWFQ), Delay = 25 ms,
 *   Buffer     = 100 packets, Packet size = 1500 bytes, RTT ~ 58 ms
 *
 * Output: throughput_fig11_newreno.tr
 *   Columns:  sim_time_s   bytes_per_second   elapsed_s_since_start
 */

#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig11NewRenoThroughput");

static uint64_t g_lastRxBytes  = 0;
static double   g_sampleWindow = 1.0;   

static void ThroughputSample(Ptr<PacketSink> sink, Ptr<OutputStreamWrapper> stream)
{
    uint64_t rxBytes   = sink->GetTotalRx();
    uint64_t diffBytes = rxBytes - g_lastRxBytes;
    g_lastRxBytes      = rxBytes;

    double nowSec     = Simulator::Now().GetSeconds();
    double elapsedSec = nowSec - 1.0;   // seconds since sender start

    // bytes per second (normalise by window size)
    double bytesPerSec = static_cast<double>(diffBytes) / g_sampleWindow;

    *stream->GetStream() << nowSec
                         << "\t" << bytesPerSec
                         << "\t" << elapsedSec
                         << std::endl;

    Simulator::Schedule(Seconds(g_sampleWindow),
                        &ThroughputSample, sink, stream);
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // TCP New Reno
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // ------------------- Nodes -------------------
    NodeContainer nodes;
    nodes.Create(2);

    // ------------------- Link -------------------
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate",   StringValue("40Mbps"));
    p2p.SetChannelAttribute("Delay",     StringValue("25ms"));
    p2p.SetQueue("ns3::DropTailQueue",   "MaxSize", StringValue("100p"));

    NetDeviceContainer devices = p2p.Install(nodes);

    // ------------------- Internet -------------------
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // ------------------- Sink -------------------
    uint16_t port = 8080;
    Address  sinkAddr(InetSocketAddress(interfaces.GetAddress(1), port));

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(50.0));

    // ------------------- Sender -------------------
    // 100 Mbps into a 40 Mbps link → buffer fills → drops → sawtooth
    Ptr<Socket>      tcpSocket =
        Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(tcpSocket, sinkAddr,
               1500,
               2000000,
               DataRate("100Mbps"));
    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(50.0));

    // ------------------- Throughput tracing -------------------
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> tpStream =
        ascii.CreateFileStream("figure11_throughput_newreno.tr");

    Ptr<PacketSink> sink =
        DynamicCast<PacketSink>(sinkApp.Get(0));

    Simulator::Schedule(Seconds(1.0 + g_sampleWindow),
                        &ThroughputSample, sink, tpStream);

    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}