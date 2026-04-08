#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SixthScriptExample");


// =======================================================
// TutorialApp (embedded)
// =======================================================
class TutorialApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("TutorialApp")
            .SetParent<Application>()
            .AddConstructor<TutorialApp>();
        return tid;
    }

    TutorialApp() {}

    void Setup(Ptr<Socket> socket, Address address,
               uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
    {
        m_socket = socket;
        m_peer = address;
        m_packetSize = packetSize;
        m_nPackets = nPackets;
        m_dataRate = dataRate;
    }

private:
    virtual void StartApplication()
    {
        m_running = true;
        m_packetsSent = 0;
        m_socket->Bind();
        m_socket->Connect(m_peer);
        SendPacket();
    }

    virtual void StopApplication()
    {
        m_running = false;
        if (m_socket) m_socket->Close();
    }

    void SendPacket()
    {
        m_socket->Send(Create<Packet>(m_packetSize));
        m_packetsSent++;

        if (m_packetsSent < m_nPackets)
        {
            Time tNext(Seconds(m_packetSize * 8.0 / m_dataRate.GetBitRate()));
            Simulator::Schedule(tNext, &TutorialApp::SendPacket, this);
        }
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    uint32_t m_nPackets;
    DataRate m_dataRate;
    bool m_running;
    uint32_t m_packetsSent;
};


// =======================================================
// BR-TCP (FULL IMPLEMENTATION)
// =======================================================
class BrTcp : public TcpLinuxReno
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::BrTcp")
                .SetParent<TcpLinuxReno>()
                .AddConstructor<BrTcp>();
        return tid;
    }

    BrTcp() : m_bandwidth(DataRate("40Mbps")),
              m_smoothRtt(Seconds(0)),
              m_prevRtt(Seconds(0)),
              m_initialized(false),
              m_rttIncreasing(false) {}

    BrTcp(const BrTcp& sock) : TcpLinuxReno(sock) {}

    Ptr<TcpCongestionOps> Fork() override
    {
        return CopyObject<BrTcp>(this);
    }

    std::string GetName() const override
    {
        return "BrTcp";
    }

    // ================= RTT Tracking =================
    void PktsAcked(Ptr<TcpSocketState> tcb,
                   uint32_t segmentsAcked,
                   const Time& rtt) override
    {
        if (!rtt.IsPositive()) return;

        double alpha = 0.125;

        if (!m_initialized)
        {
            m_smoothRtt = rtt;
            m_prevRtt = rtt;
            m_initialized = true;
        }
        else
        {
            double smooth = (1 - alpha) * m_smoothRtt.GetSeconds() + alpha * rtt.GetSeconds();
            m_smoothRtt = Seconds(smooth);
        }

        // RTT trend detection
        if (m_smoothRtt > m_prevRtt)
            m_rttIncreasing = true;
        else
            m_rttIncreasing = false;

        m_prevRtt = m_smoothRtt;

        TcpLinuxReno::PktsAcked(tcb, segmentsAcked, rtt);
    }

    // ================= Loss Differentiation =================
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight) override
    {
        uint32_t mss = tcb->m_segmentSize;

        double r = m_bandwidth.GetBitRate();
        double rtt = m_smoothRtt.GetSeconds();

        uint32_t brThresh = (uint32_t)((r * rtt) / 8.0);
        brThresh = std::max(brThresh, 2 * mss);
        brThresh = (brThresh / mss) * mss;

        uint32_t renoThresh = std::max(bytesInFlight / 2, 2 * mss);

        //  LOSS DIFFERENTIATION
        if (m_rttIncreasing)
        {
            // congestion loss
            return brThresh;
        }
        else
        {
            // random loss
            return std::max(brThresh, renoThresh);
        }
    }

    // ================= Fairness =================
    void IncreaseWindow(Ptr<TcpSocketState> tcb,
                        uint32_t segmentsAcked) override
    {
        TcpLinuxReno::IncreaseWindow(tcb, segmentsAcked);

        uint32_t cwndReno = tcb->m_cWnd;

        double r = m_bandwidth.GetBitRate();
        double rtt = m_smoothRtt.GetSeconds();

        uint32_t cwndBR = (uint32_t)((r * rtt) / 8.0);

        uint32_t newCwnd = std::min(cwndReno, cwndBR);
        newCwnd = std::max(newCwnd, tcb->m_segmentSize);

        tcb->m_cWnd = newCwnd;
    }

private:
    DataRate m_bandwidth;

    Time m_smoothRtt;
    Time m_prevRtt;

    bool m_initialized;
    bool m_rttIncreasing;
};


// =======================================================
// Tracing
// =======================================================
static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newCwnd / 1500.0 << std::endl;
}

static void SsthreshChange(Ptr<OutputStreamWrapper> stream,
                           uint32_t oldVal, uint32_t newVal)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newVal / 1500.0 << std::endl;
}

static void RttChange(Ptr<OutputStreamWrapper> stream,
                      Time oldRtt, Time newRtt)
{
    *stream->GetStream() << Simulator::Now().GetSeconds()
                         << "\t" << newRtt.GetSeconds() << std::endl;
}


// =======================================================
// MAIN
// =======================================================
int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    //  Use BR-TCP
    TypeId tid = BrTcp::GetTypeId();
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(BrTcp::GetTypeId()));

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1500));

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("40Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("25ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    auto interfaces = address.Assign(devices);

    uint16_t port = 8080;

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));

    auto sinkApp = sinkHelper.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0));
    sinkApp.Stop(Seconds(50));

    Ptr<Socket> tcpSocket =
        Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

    Ptr<TutorialApp> app = CreateObject<TutorialApp>();
    app->Setup(tcpSocket,
               InetSocketAddress(interfaces.GetAddress(1), port),
               1500,
               2000000,
               DataRate("100Mbps"));

    nodes.Get(0)->AddApplication(app);
    app->SetStartTime(Seconds(1));
    app->SetStopTime(Seconds(50));

    AsciiTraceHelper ascii;

    tcpSocket->TraceConnectWithoutContext(
        "CongestionWindow",
        MakeBoundCallback(&CwndChange, ascii.CreateFileStream("figure_improved_cwnd.tr")));

    tcpSocket->TraceConnectWithoutContext(
        "SlowStartThreshold",
        MakeBoundCallback(&SsthreshChange, ascii.CreateFileStream("figure_improved_ssthresh.tr")));

    tcpSocket->TraceConnectWithoutContext(
        "RTT",
        MakeBoundCallback(&RttChange, ascii.CreateFileStream("figure_improved_rtt.tr")));

    Simulator::Stop(Seconds(50));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}