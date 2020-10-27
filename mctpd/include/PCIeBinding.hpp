#pragma once

#include "MCTPBinding.hpp"

#include <libmctp-nupcie.h>

#include <xyz/openbmc_project/MCTP/Binding/PCIe/server.hpp>

using pcie_binding =
    sdbusplus::xyz::openbmc_project::MCTP::Binding::server::PCIe;

class PCIeBinding : public MctpBinding
{
  public:
    PCIeBinding() = delete;
    PCIeBinding(std::shared_ptr<object_server>& objServer, std::string& objPath,
                ConfigurationVariant& conf, boost::asio::io_context& ioc);
    virtual ~PCIeBinding();
    virtual void initializeBinding(ConfigurationVariant& conf) override;

  private:
    uint16_t bdf;
    pcie_binding::DiscoveryFlags discoveredFlag{};
    struct mctp_binding_nupcie* pcie = nullptr;
    boost::asio::posix::stream_descriptor streamMonitor;
    bool endpointDiscoveryFlow();
    void readResponse();
    static void rxCTXMessage(uint8_t srcEid, void* data, void* msg,
               size_t len, void* binding_private);
    void Eid(uint8_t value);
    void DiscoveredFlag(std::string value);
    void Bdf(uint16_t value);
};
