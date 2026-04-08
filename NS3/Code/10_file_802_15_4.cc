#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/lr-wpan-net-device.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"
#include "ns3/ripng-helper.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/tcp-congestion-ops.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WpanStaticTraces");

class TcpBr : public TcpNewReno
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::TcpBr")
                .SetParent<TcpNewReno>()
                .SetGroupName("Internet")
                .AddConstructor<TcpBr>()
                .AddAttribute(
                    "ReservedBandwidth",
                    "Reserved bandwidth [bit/s] for BR-TCP ssthresh formula",
                    DataRateValue(DataRate("250kbps")),
                    MakeDataRateAccessor(&TcpBr::m_reservedBw),
                    MakeDataRateChecker());
        return tid;
    }

    TcpBr() : TcpNewReno(), m_reservedBw("250kbps") {}
    TcpBr(const TcpBr& s) : TcpNewReno(s), m_reservedBw(s.m_reservedBw) {}
    ~TcpBr() override = default;

    std::string GetName() const override { return "TcpBr"; }

    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight) override
    {
        double   rtt = tcb->m_lastRtt.Get().GetSeconds();
        double   r   = static_cast<double>(m_reservedBw.GetBitRate());
        uint32_t seg = tcb->m_segmentSize;

        if (rtt > 0.0 && r > 0.0 && seg > 0)
        {
            uint32_t ss = static_cast<uint32_t>((r / 8.0) * rtt);
            return std::max(ss, 2 * seg);
        }
        return TcpNewReno::GetSsThresh(tcb, bytesInFlight);
    }

    Ptr<TcpCongestionOps> Fork() override
    {
        return CopyObject<TcpBr>(this);
    }

private:
    DataRate m_reservedBw;
};

NS_OBJECT_ENSURE_REGISTERED(TcpBr);

static void CwndChange(Ptr<OutputStreamWrapper> stream,
                       uint32_t /*old*/, uint32_t newCwnd)
{
    // 60 bytes = segment size → packets এ convert
    *stream->GetStream()
        << Simulator::Now().GetSeconds()
        << "\t" << newCwnd / 60.0
        << "\n";
}

static void SsthreshChange(Ptr<OutputStreamWrapper> stream,
                            uint32_t /*old*/, uint32_t newSS)
{
  
    if (newSS >= 0x7fffffff) return;

    *stream->GetStream()
        << Simulator::Now().GetSeconds()
        << "\t" << newSS / 60.0
        << "\n";
}

static void RunOneConfig(uint32_t nNodes,
                         uint32_t nFlows,
                         uint32_t pktPerSec,
                         double   txRange,
                         double   simTime)
{
    std::cout << "\n>>> wpan n=" << nNodes
              << " flows=" << nFlows
              << " pkt/s=" << pktPerSec << " <<<"
              << std::endl;

    const uint32_t pktSize   = 60;    
    const double   startTime = 10.0;  // RIPng convergence time
    const double   areaSide  = txRange; // 1× coverage

    /* ── 1. Nodes ── */
    NodeContainer nodes;
    nodes.Create(nNodes);

    LrWpanHelper lrWpan;

    Ptr<SingleModelSpectrumChannel> channel =
        CreateObject<SingleModelSpectrumChannel>();

    Ptr<RangePropagationLossModel> propLoss =
        CreateObject<RangePropagationLossModel>();
    propLoss->SetAttribute("MaxRange", DoubleValue(txRange));
    channel->AddPropagationLossModel(propLoss);

    Ptr<ConstantSpeedPropagationDelayModel> propDelay =
        CreateObject<ConstantSpeedPropagationDelayModel>();
    channel->SetPropagationDelayModel(propDelay);

    lrWpan.SetChannel(channel);
    NetDeviceContainer lrDevices = lrWpan.Install(nodes);

    // PAN ID + Short address assignment
    for (uint32_t i = 0; i < lrDevices.GetN(); i++)
    {
        Ptr<LrWpanNetDevice> dev =
            DynamicCast<LrWpanNetDevice>(lrDevices.Get(i));
        dev->GetMac()->SetPanId(0);
        uint8_t addrBuf[2];
        addrBuf[0] = (uint8_t)((i + 1) >> 8);
        addrBuf[1] = (uint8_t)((i + 1) & 0xFF);
        Mac16Address shortAddr;
        shortAddr.CopyFrom(addrBuf);
        dev->GetMac()->SetShortAddress(shortAddr);
    }

    /* ── 3. 6LoWPAN ── */
    SixLowPanHelper sixLowPan;
    sixLowPan.SetDeviceAttribute("ForceEtherType", BooleanValue(true));
    NetDeviceContainer sixDevices = sixLowPan.Install(lrDevices);

    /* ── 4. Mobility — grid layout (predictable connectivity) ── */
    MobilityHelper mobility;
    uint32_t gridW   = (uint32_t)std::ceil(std::sqrt((double)nNodes));
    double   spacing = std::min(txRange * 0.8, areaSide / (double)gridW);

    mobility.SetPositionAllocator(
        "ns3::GridPositionAllocator",
        "MinX",       DoubleValue(0.0),
        "MinY",       DoubleValue(0.0),
        "DeltaX",     DoubleValue(spacing),
        "DeltaY",     DoubleValue(spacing),
        "GridWidth",  UintegerValue(gridW),
        "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    /* ── 5. Internet stack + RIPng ──
     * BUG FIX 1 (from multirun): single declaration, no duplication */
    RipNgHelper ripNg;
    Ipv6ListRoutingHelper listRH;
    listRH.Add(ripNg, 0);

    InternetStackHelper internet;
    internet.SetRoutingHelper(listRH);
    internet.Install(nodes);

    /* ── 6. IPv6 addressing ── */
    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer ifaces = ipv6.Assign(sixDevices);

    for (uint32_t i = 0; i < ifaces.GetN(); i++)
        ifaces.SetForwarding(i, true);

    uint32_t src0 = 0;
    uint32_t dst0 = nNodes / 2;
    uint16_t port0 = 9000;

    PacketSinkHelper sink0(
        "ns3::TcpSocketFactory",
        Inet6SocketAddress(Ipv6Address::GetAny(), port0));
    ApplicationContainer sinkApp0 = sink0.Install(nodes.Get(dst0));
    sinkApp0.Start(Seconds(0.0));
    sinkApp0.Stop(Seconds(simTime));

    BulkSendHelper bulk0(
        "ns3::TcpSocketFactory",
        Inet6SocketAddress(ifaces.GetAddress(dst0, 1), port0));
    bulk0.SetAttribute("MaxBytes", UintegerValue(0));    // unlimited
    bulk0.SetAttribute("SendSize", UintegerValue(pktSize));

    ApplicationContainer srcApp0 = bulk0.Install(nodes.Get(src0));
    srcApp0.Start(Seconds(startTime));
    srcApp0.Stop(Seconds(simTime - 1.0));

    /* ── 8. Background flows: OnOff (not traced) ── */
    const std::string bgRate =
        std::to_string(pktSize * 8 * pktPerSec) + "bps";

    for (uint32_t i = 1; i < nFlows; i++)
    {
        uint32_t src  = i % nNodes;
        uint32_t dst  = (i + nNodes / 2) % nNodes;
        if (src == dst) dst = (dst + 1) % nNodes;
        uint16_t port = 9000 + i;

        PacketSinkHelper sink(
            "ns3::TcpSocketFactory",
            Inet6SocketAddress(Ipv6Address::GetAny(), port));
        sink.Install(nodes.Get(dst)).Start(Seconds(0.0));

        OnOffHelper onoff(
            "ns3::TcpSocketFactory",
            Inet6SocketAddress(ifaces.GetAddress(dst, 1), port));
        onoff.SetAttribute("DataRate",   StringValue(bgRate));
        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
        onoff.SetAttribute("OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onoff.Install(nodes.Get(src)).Start(Seconds(startTime + 1.0));
    }

    /* ── 9. Trace files ── */
    std::string tag = "wpan_n" + std::to_string(nNodes);

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> cwndStream =
        ascii.CreateFileStream(tag + "_cwnd.tr");
    Ptr<OutputStreamWrapper> ssStream =
        ascii.CreateFileStream(tag + "_ssthresh.tr");

    Simulator::Schedule(
        Seconds(startTime + 1.0),
        [nodes, src0, cwndStream, ssStream]()
        {
            Ptr<Node> node = nodes.Get(src0);
            Ptr<TcpL4Protocol> tcp = node->GetObject<TcpL4Protocol>();
            if (!tcp)
            {
                std::cerr << "[WPAN] No TCP on node 0!\n";
                return;
            }

            ObjectVectorValue sockets;
            tcp->GetAttribute("SocketList", sockets);

            if (sockets.GetN() == 0)
            {
                std::cerr << "[WPAN] No socket on node 0 yet!\n";
                return;
            }

            Ptr<TcpSocketBase> tcpSock =
                DynamicCast<TcpSocketBase>(sockets.Get(0));
            if (!tcpSock)
            {
                std::cerr << "[WPAN] Cannot cast to TcpSocketBase!\n";
                return;
            }

            tcpSock->TraceConnectWithoutContext(
                "CongestionWindow",
                MakeBoundCallback(&CwndChange, cwndStream));

            tcpSock->TraceConnectWithoutContext(
                "SlowStartThreshold",
                MakeBoundCallback(&SsthreshChange, ssStream));

            std::cout << "    [WPAN] Trace connected for n="
                      << nodes.GetN() << std::endl;
        });

    /* ── 11. Run ── */
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "    Saved: " << tag << "_cwnd.tr" << std::endl;
    std::cout << "    Saved: " << tag << "_ssthresh.tr" << std::endl;
}

/* =====================================================================
 * Main
 * ===================================================================== */
int main(int argc, char* argv[])
{
    /* Global TCP settings for 802.15.4 */
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue("ns3::TcpBr"));
    Config::SetDefault("ns3::TcpBr::ReservedBandwidth",
                       DataRateValue(DataRate("250kbps")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(60));           // 802.15.4 MTU
    Config::SetDefault("ns3::TcpSocket::SndBufSize",
                       UintegerValue(1 << 16));      // 64 KB
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                       UintegerValue(1 << 16));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(1));
    Config::SetDefault("ns3::TcpSocket::DelAckCount",
                       UintegerValue(1));

    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    const double   txRange   = 30.0;   // 802.15.4 range (meters)
    const double   simTime   = 100.0;  // longer: RIPng needs time
    const uint32_t nFlows    = 10;
    const uint32_t pktPerSec = 10;     

    // 5 node configurations → 10 .tr files
    uint32_t nodeConfigs[] = {20, 40, 60, 80, 100};

    std::cout << "=================================================\n";
    std::cout << " IEEE 802.15.4 BR-TCP — 10 Trace Files\n";
    std::cout << " TxRange=" << txRange << "m"
              << "  SimTime=" << simTime << "s"
              << "  Flows=" << nFlows
              << "  PktPerSec=" << pktPerSec << "\n";
    std::cout << "=================================================\n";

    for (uint32_t n : nodeConfigs)
        RunOneConfig(n, nFlows, pktPerSec, txRange, simTime);

    std::cout << "\n=================================================\n";
    std::cout << " Done! Files generated:\n";
    for (uint32_t n : nodeConfigs)
    {
        std::cout << "   wpan_n" << n << "_cwnd.tr\n";
        std::cout << "   wpan_n" << n << "_ssthresh.tr\n";
    }
    std::cout << "=================================================\n";

    return 0;
}