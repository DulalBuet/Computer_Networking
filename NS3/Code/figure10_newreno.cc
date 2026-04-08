/* sixth_fig10_newreno.cc
 * Reproduces the testbed experiment of Figure 10 (Section 5.2) in NS-3.
 * Logs the congestion window of TCP New Reno.
 *
 * Testbed parameters (paper Section 5):
 *   Bandwidth  = 40 Mbps (CBWFQ-limited), Delay = 25 ms,
 *   Buffer     = 100 packets, Packet size = 1500 bytes, RTT ~ 58 ms
 *
 * Key difference from Figure 4/6 simulation:
 *   The testbed uses MPLS with CBWFQ bandwidth shaping, modelled here
 *   as a strict 40 Mbps DropTail bottleneck — same topology but the
 *   sender is also capped at 40 Mbps (not 100 Mbps) to better match
 *   the hardware CBQ/CBWFQ enforcement seen in the real testbed results.
 *   This produces the tighter, lower cwnd swings seen in Figure 10
 *   compared to Figure 6.
 *
 * Output: cwnd_fig10_newreno.tr  (time [s]  cwnd [packets])
 */

#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Fig10NewRenoTestbed");

static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newCwnd / 1500.0 << std::endl;
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
    // Testbed: CBWFQ enforces 40 Mbps; model both link AND sender at 40 Mbps
    // to replicate the tighter hardware-limited cwnd swings of Figure 10
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
    // Send at exactly 40 Mbps (matches CBWFQ hardware rate limiting)
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

    // ------------------- CWND Tracing -------------------
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> cwndStream =
        ascii.CreateFileStream("figure10_cwnd_newreno.tr");
    tcpSocket->TraceConnectWithoutContext("CongestionWindow",
        MakeBoundCallback(&CwndChange, cwndStream));

    // ------------------- Run -------------------
    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}