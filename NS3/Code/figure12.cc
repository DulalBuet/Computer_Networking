/* sixth_fig12_brtcp.cc
 * Reproduces Figure 12 (Section 5.2 testbed) in NS-3.
 * Logs throughput of BR-TCP in bytes per second.
 *
 * Figure 12 vs Figure 11:
 *   - Same axes: Y = Throughput [byte], 0-10M; X = wall-clock timestamps
 *   - BR-TCP line is HIGHER and MORE STABLE than New Reno (Fig 11)
 *   - Fewer deep throughput drops because ssthresh is restored to
 *     ~193 packets after each loss instead of being halved
 *   - Average throughput BR-TCP ~3.4M bytes/s vs New Reno ~2.5M bytes/s
 *     (paper Section 5.2)
 *
 * BR-TCP ssthresh formula:
 *   ssthresh = (r / (size * 8)) * RTT
 *            = (40e6 / 12000) * 0.058
 *            ≈ 193 packets = 289,500 bytes
 *
 * Testbed parameters (Section 5):
 *   Bandwidth  = 40 Mbps (CBWFQ), Delay = 25 ms,
 *   Buffer     = 100 packets, Packet size = 1500 bytes, RTT ~ 58 ms
 *
 * Output: throughput_fig12_brtcp.tr
 *   Columns:  sim_time_s   bytes_per_second   elapsed_s_since_start
 */

#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig12BRTCPThroughput");

static const double RESERVED_BW_BPS   = 40e6;
static const double PACKET_SIZE_BYTES = 1500.0;
static const double MIN_RTT_SEC       = 0.058;   // 58 ms

static uint32_t ComputeBRTCPSsthresh()
{
    double pkts = (RESERVED_BW_BPS / (PACKET_SIZE_BYTES * 8.0)) * MIN_RTT_SEC;
    return static_cast<uint32_t>(pkts * PACKET_SIZE_BYTES);  // bytes
}

static uint64_t g_lastRxBytes  = 0;
static double   g_sampleWindow = 1.0;   // 1-second window → bytes/s


static void SsthreshChange(Ptr<TcpSocketBase> socket, uint32_t /*oldVal*/, uint32_t /*newVal*/)
{
    uint32_t brtcpSsthresh = ComputeBRTCPSsthresh();
    Simulator::Schedule(MicroSeconds(1), [socket, brtcpSsthresh]() {
        socket->SetAttribute("InitialSlowStartThreshold",
                             UintegerValue(brtcpSsthresh));
    });
}

static void ThroughputSample(Ptr<PacketSink> sink, Ptr<OutputStreamWrapper> stream)
{
    uint64_t rxBytes   = sink->GetTotalRx();
    uint64_t diffBytes = rxBytes - g_lastRxBytes;
    g_lastRxBytes      = rxBytes;

    double nowSec     = Simulator::Now().GetSeconds();
    double elapsedSec = nowSec - 1.0;   // seconds since sender start

    double bytesPerSec = static_cast<double>(diffBytes) / g_sampleWindow;

    *stream->GetStream() << nowSec
                         << "\t" << bytesPerSec
                         << "\t" << elapsedSec
                         << std::endl;

    Simulator::Schedule(Seconds(g_sampleWindow), &ThroughputSample, sink, stream);
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // Pre-set ssthresh to BR-TCP value (~193 packets = 289,500 bytes)
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold",
                       UintegerValue(ComputeBRTCPSsthresh()));

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
    // 100 Mbps into 40 Mbps link → forces congestion and buffer drops
    // BR-TCP recovers faster → higher stable throughput vs New Reno
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

    // ------------------- BR-TCP ssthresh hook -------------------
    Ptr<TcpSocketBase> tcpBase = DynamicCast<TcpSocketBase>(tcpSocket);
    tcpBase->TraceConnectWithoutContext("SlowStartThreshold",
        MakeBoundCallback(&SsthreshChange, tcpBase));

    // ------------------- Throughput tracing -------------------
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> tpStream =
        ascii.CreateFileStream("figure12_throughput_brtcp.tr");

    Ptr<PacketSink> sink =
        DynamicCast<PacketSink>(sinkApp.Get(0));

    Simulator::Schedule(Seconds(1.0 + g_sampleWindow),
                        &ThroughputSample, sink, tpStream);

    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}