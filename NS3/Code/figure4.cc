#include "tutorial-app.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SixthScriptExample");

// ------------------- CWND -------------------
static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t oldCwnd, uint32_t newCwnd)
{
    // Divide by 1500 to track in packets, matching the paper's Y-axis
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newCwnd / 1500.0 << std::endl;
}

// ------------------- SSTHRESH -------------------
static void SsthreshChange(Ptr<OutputStreamWrapper> stream,
                           uint32_t oldVal, uint32_t newVal)
{
    // Divide by 1500 to track in packets
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newVal / 1500.0 << std::endl;
}

// ------------------- RTT -------------------
static void RttChange(Ptr<OutputStreamWrapper> stream,
                      Time oldRtt, Time newRtt)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newRtt.GetSeconds() << std::endl;
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Force TCP NewReno
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));

    
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(10000000));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // ------------------- Nodes -------------------
    NodeContainer nodes;
    nodes.Create(2);

    // ------------------- Link (MATCH PAPER) -------------------
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("40Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("25ms"));

    // Paper buffer size is 100 packets
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

    // Removed the arbitrary RateErrorModel. Packet drops will now naturally 
    // occur when the router's 100-packet buffer overflows, triggering NewReno.

    app->Setup(tcpSocket, sinkAddress,
               1500,               
               2000000, // Make sure we don't run out of packets    
               DataRate("100Mbps")); // Force higher than 40Mbps to cause congestion!

    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(50.0));   

    // ------------------- Tracing -------------------
    AsciiTraceHelper ascii;

    Ptr<OutputStreamWrapper> cwndStream = ascii.CreateFileStream("figure4_cwnd.tr");
    tcpSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback(&CwndChange, cwndStream));

    Ptr<OutputStreamWrapper> ssthreshStream = ascii.CreateFileStream("figure4_ssthresh.tr");
    tcpSocket->TraceConnectWithoutContext("SlowStartThreshold", MakeBoundCallback(&SsthreshChange, ssthreshStream));

    Ptr<OutputStreamWrapper> rttStream = ascii.CreateFileStream("figure4_rtt.tr");
    tcpSocket->TraceConnectWithoutContext("RTT", MakeBoundCallback(&RttChange, rttStream));


    Simulator::Stop(Seconds(50));   
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}