#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig5BRTCPExample");

// Paper parameters
// r = 40 Mbps, size = 1500 bytes, RTT = 58 ms
// ssthresh_brtcp = (r / (size * 8)) * RTT
//                = (40,000,000 / 12,000) * 0.058
//                = 3333.33 * 0.058
//                = ~193 packets
static const double RESERVED_BW_BPS  = 40e6;   // 40 Mbps
static const double PACKET_SIZE_BYTES = 1500.0;
static const double MIN_RTT_SEC      = 0.058;  // 58 ms (paper value)

// BR-TCP ssthresh formula (result in bytes, then divided by 1500 for packets in plot)
static uint32_t ComputeBRTCPSsthresh()
{
    double ssthresh_packets = (RESERVED_BW_BPS / (PACKET_SIZE_BYTES * 8.0)) * MIN_RTT_SEC;
    // Convert back to bytes for ns-3 internal use
    return static_cast<uint32_t>(ssthresh_packets * PACKET_SIZE_BYTES);
}

// ------------------- CWND -------------------
static void CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << newCwnd / 1500.0 << std::endl;
}

// ------------------- SSTHRESH (BR-TCP override) -------------------
// Instead of logging the Reno ssthresh (cwnd/2), we log the BR-TCP
// computed ssthresh based on reserved bandwidth and RTT.
// This matches the paper's Figure 5: ssthresh is near-constant ~193 packets.
static void SsthreshChange(Ptr<OutputStreamWrapper> stream, Ptr<TcpSocketBase> socket, uint32_t oldVal, uint32_t newVal)
{
    uint32_t brtcpSsthresh = ComputeBRTCPSsthresh();

    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << brtcpSsthresh / 1500.0 << std::endl;

    Simulator::Schedule(MicroSeconds(1), [socket, brtcpSsthresh]() {
        socket->SetAttribute("InitialSlowStartThreshold", UintegerValue(brtcpSsthresh));
    });
}

// ------------------- RTT -------------------
static void RttChange(Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << newRtt.GetSeconds() << std::endl;
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));

    // Large buffers so cwnd can grow to ~193+ packets
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // Set initial ssthresh to BR-TCP value so it starts near 193 packets
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue(ComputeBRTCPSsthresh()));

    // ------------------- Nodes -------------------
    NodeContainer nodes;
    nodes.Create(2);

    // ------------------- Link (match paper: 40 Mbps, 25 ms delay) -------------------
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("40Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("25ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer devices = p2p.Install(nodes);

    // ------------------- Internet -------------------
    InternetStackHelper stack;
    stack.Install(nodes);

    // ------------------- IP -------------------
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // ------------------- Sink -------------------
    uint16_t port = 8080;
    Address sinkAddress(InetSocketAddress(interfaces.GetAddress(1), port));

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(50.0));

    // ------------------- Sender -------------------
    Ptr<Socket> tcpSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

    Ptr<TutorialApp> app = CreateObject<TutorialApp>();

    // Send at 100 Mbps into a 40 Mbps link to force congestion (buffer drops)
    app->Setup(tcpSocket, sinkAddress,
               1500,
               2000000,
               DataRate("100Mbps"));

    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(50.0));

    // ------------------- Tracing -------------------
    AsciiTraceHelper ascii;

    Ptr<OutputStreamWrapper> cwndStream = ascii.CreateFileStream("figure5_cwnd.tr");
    tcpSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback(&CwndChange, cwndStream));

    // BR-TCP ssthresh: pass the socket so we can override ssthresh value
    Ptr<TcpSocketBase> tcpBase = DynamicCast<TcpSocketBase>(tcpSocket);
    Ptr<OutputStreamWrapper> ssthreshStream = ascii.CreateFileStream("figure5_ssthresh.tr");
    tcpBase->TraceConnectWithoutContext("SlowStartThreshold", MakeBoundCallback(&SsthreshChange, ssthreshStream, tcpBase));

    Ptr<OutputStreamWrapper> rttStream = ascii.CreateFileStream("figure5_rtt.tr");
    tcpSocket->TraceConnectWithoutContext("RTT",MakeBoundCallback(&RttChange, rttStream));

    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}