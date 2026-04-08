/* sixth_fig8_newreno.cc
 * Logs throughput of TCP New Reno for Figure 8.
 * Periodically samples bytes received at the PacketSink and computes
 * instantaneous throughput in Mbps over a 0.5s sliding window.
 *
 * Parameters matching the paper:
 *   Bandwidth  = 40 Mbps, Delay = 25 ms, Buffer = 100 packets,
 *   Packet size = 1500 bytes, RTT ~ 58 ms
 *
 * Output: throughput_newreno.tr  (time [s]  throughput [Mbps])
 */

#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig8NewRenoThroughput");

static uint64_t g_lastRxBytes  = 0;
static double   g_sampleWindow = 0.5;   // seconds per sample

static void ThroughputSample(Ptr<PacketSink> sink, Ptr<OutputStreamWrapper> stream)
{
    uint64_t rxBytes  = sink->GetTotalRx();
    uint64_t diffBytes = rxBytes - g_lastRxBytes;
    g_lastRxBytes      = rxBytes;

    // Convert bytes/window to Mbps
    double throughputMbps = (diffBytes * 8.0) / (g_sampleWindow * 1e6);

    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << throughputMbps << std::endl;

    // Reschedule
    Simulator::Schedule(Seconds(g_sampleWindow),
                        &ThroughputSample, sink, stream);
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // TCP New Reno
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
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
    Ptr<Socket>      tcpSocket =
        Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(tcpSocket, sinkAddr,
               1500,
               2000000,
               DataRate("100Mbps"));   // > 40 Mbps to force congestion
    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(50.0));

    // ------------------- Throughput tracing -------------------
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> tpStream =
        ascii.CreateFileStream("figure8_throughput_newreno.tr");

    // Get the PacketSink pointer for periodic sampling
    Ptr<PacketSink> sink =
        DynamicCast<PacketSink>(sinkApp.Get(0));

    // Start sampling 1 s after sender starts
    Simulator::Schedule(Seconds(1.0 + g_sampleWindow), &ThroughputSample, sink, tpStream);

    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}