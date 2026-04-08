/* sixth_fig10_brtcp.cc
 * Reproduces the testbed experiment of Figure 10 (Section 5.2) in NS-3.
 * Logs the congestion window of BR-TCP.
 *
 * Testbed parameters (paper Section 5):
 *   Bandwidth  = 40 Mbps (CBWFQ-limited), Delay = 25 ms,
 *   Buffer     = 100 packets, Packet size = 1500 bytes, RTT ~ 58 ms
 *
 * BR-TCP ssthresh formula:
 *   ssthresh = (r / (size * 8)) * RTT
 *            = (40e6 / 12000) * 0.058
 *            ≈ 193 packets
 *
 * In Figure 10 BR-TCP's cwnd swings are WIDER than New Reno (it climbs
 * higher before each drop) because ssthresh is held near 193 packets —
 * the window grows up to ~200 before the buffer fills, then resets to
 * ssthresh rather than halving, so it recovers fast.
 *
 * Output: cwnd_fig10_brtcp.tr  (time [s]  cwnd [packets])
 */

#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig10BRTCPTestbed");

static const double RESERVED_BW_BPS   = 40e6;
static const double PACKET_SIZE_BYTES = 1500.0;
static const double MIN_RTT_SEC       = 0.058;   // 58 ms

static uint32_t ComputeBRTCPSsthresh()
{
    double pkts = (RESERVED_BW_BPS / (PACKET_SIZE_BYTES * 8.0)) * MIN_RTT_SEC;
    return static_cast<uint32_t>(pkts * PACKET_SIZE_BYTES);  // bytes
}


static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newCwnd / 1500.0 << std::endl;
}


static void SsthreshChange(Ptr<TcpSocketBase> socket,
                           uint32_t /*oldVal*/, uint32_t /*newVal*/)
{
    uint32_t brtcpSsthresh = ComputeBRTCPSsthresh();
    Simulator::Schedule(MicroSeconds(1), [socket, brtcpSsthresh]() {
        socket->SetAttribute("InitialSlowStartThreshold",
                             UintegerValue(brtcpSsthresh));
    });
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Base: TCP NewReno (BR-TCP overrides ssthresh via callback)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",  UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // Pre-set ssthresh to BR-TCP value (~193 packets = 289,800 bytes)
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
    Ptr<Socket>      tcpSocket =
        Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(tcpSocket, sinkAddr,
               1500,
               2000000,
               DataRate("40Mbps"));   // hardware rate-limited as in testbed
    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(50.0));

    // ------------------- BR-TCP ssthresh hook -------------------
    Ptr<TcpSocketBase> tcpBase = DynamicCast<TcpSocketBase>(tcpSocket);
    tcpBase->TraceConnectWithoutContext("SlowStartThreshold",
        MakeBoundCallback(&SsthreshChange, tcpBase));

    // ------------------- CWND Tracing -------------------
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> cwndStream =
        ascii.CreateFileStream("figure10_cwnd_brtcp.tr");
    tcpSocket->TraceConnectWithoutContext("CongestionWindow",
        MakeBoundCallback(&CwndChange, cwndStream));


    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}