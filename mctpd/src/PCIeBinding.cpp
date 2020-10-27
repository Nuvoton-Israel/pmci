#include "PCIeBinding.hpp"

#include <phosphor-logging/log.hpp>

using variant = std::variant<uint8_t, uint16_t, bool, std::string>;

PCIeBinding::PCIeBinding(std::shared_ptr<object_server>& objServer,
                         std::string& objPath, ConfigurationVariant& conf,
                         boost::asio::io_context& ioc) :
    MctpBinding(objServer, objPath, conf, ioc),
    streamMonitor(ioc)
{
    std::shared_ptr<dbus_interface> pcieInterface =
        objServer->add_interface(objPath, pcie_binding::interface);

    try
    {
        bdf = std::get<PcieConfiguration>(conf).bdf;

        if (bindingModeType == mctp_server::BindingModeTypes::BusOwner)
            discoveredFlag = pcie_binding::DiscoveryFlags::NotApplicable;
        else
            discoveredFlag = pcie_binding::DiscoveryFlags::Undiscovered;

        registerProperty(pcieInterface, "BDF", bdf);

        registerProperty(
            pcieInterface, "DiscoveredFlag",
            pcie_binding::convertDiscoveryFlagsToString(discoveredFlag));
        if (pcieInterface->initialize() == false)
        {
            throw std::system_error(
                std::make_error_code(std::errc::function_not_supported));
        }
    }
    catch (std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "MCTP PCIe Interface initialization failed.",
            phosphor::logging::entry("Exception:", e.what()));
        throw;
    }
}

void PCIeBinding::Eid(uint8_t value)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set Eid\n";
            }
        },
        "xyz.openbmc_project.MCTP-pcie",
        "/xyz/openbmc_project/mctp",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.MCTP.Base", "Eid", variant(value));

    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set StaticEid\n";
            }
        },
        "xyz.openbmc_project.MCTP-pcie",
        "/xyz/openbmc_project/mctp",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.MCTP.Base", "StaticEid", variant(false));

}

void PCIeBinding::DiscoveredFlag(std::string value)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set StaticEid\n";
            }
        },
        "xyz.openbmc_project.MCTP-pcie",
        "/xyz/openbmc_project/mctp",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.MCTP.Binding.PCIe", "DiscoveredFlag", variant(value));
}

void PCIeBinding::Bdf(uint16_t value)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set StaticEid\n";
            }
        },
        "xyz.openbmc_project.MCTP-pcie",
        "/xyz/openbmc_project/mctp",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.MCTP.Binding.PCIe", "BDF", variant(value));
}

bool PCIeBinding::endpointDiscoveryFlow()
{
    struct mctp_nupcie_pkt_private pktPrv;
    pktPrv.routing = PCIE_ROUTE_TO_RC;
    pktPrv.remote_id = bdf;
    /*
     * The workaround is temporarily needed for the current libmctp-intel
     * to determine whether the message is a request or a response.
     * Any other flag except for TO is set in libmctp.
     */
#ifdef MCTP_ASTPCIE_RESPONSE_WA
    pktPrv.flags_seq_tag = 0;
    pktPrv.flags_seq_tag |= MCTP_HDR_FLAG_TO;
#endif
    uint8_t* pktPrvPtr = reinterpret_cast<uint8_t*>(&pktPrv);
    std::vector<uint8_t> prvData =
        std::vector<uint8_t>(pktPrvPtr, pktPrvPtr + sizeof pktPrv);

    boost::asio::spawn(io, [prvData, this](boost::asio::yield_context yield) {
        if (!discoveryNotifyCtrlCmd(yield, prvData, MCTP_EID_NULL))
        {
            phosphor::logging::log<phosphor::logging::level::INFO>(
                "Discovery Notify failed");
            return false;
        }
        return true;
    });
    return false;
}

void PCIeBinding::readResponse()
{
    streamMonitor.async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [this](const boost::system::error_code& ec) {
            if (ec)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "Error reading PCIe response");
                readResponse();
            }
            mctp_nupcie_rx(pcie);
            readResponse();
        });
}

/*
 * conf can't be removed since we override virtual function that has the
 * ConfigurationVariant& as argument
 */
void PCIeBinding::initializeBinding([[maybe_unused]] ConfigurationVariant& conf)
{
    int status = 0;
    initializeMctp();
    pcie = mctp_nupcie_init();
    if (pcie == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error in MCTP PCIe init");
        throw std::system_error(
            std::make_error_code(std::errc::not_enough_memory));
    }
    struct mctp_binding* binding = mctp_nupcie_core(pcie);
    if (binding == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error in MCTP binding init");
        throw std::system_error(
            std::make_error_code(std::errc::not_enough_memory));
    }
    status = mctp_register_bus_dynamic_eid(mctp, binding);
    if (status < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Bus registration of binding failed");
        throw std::system_error(
            std::make_error_code(static_cast<std::errc>(-status)));
    }
    mctp_set_rx_all(mctp, rxMessage, this);
    mctp_set_rx_ctrl(mctp, rxCTXMessage, this);
    mctp_binding_set_tx_enabled(binding, true);

    int driverFd = mctp_nupcie_get_fd(pcie);
    if (driverFd < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error opening driver file");
        throw std::system_error(
            std::make_error_code(std::errc::not_enough_memory));
    }
    streamMonitor.assign(driverFd);
    readResponse();

    if (bindingModeType == mctp_server::BindingModeTypes::Endpoint)
    {
        boost::asio::post(io, [this]() {
            if (!endpointDiscoveryFlow())
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "Send Discovery Notify Error");
            }
        });
    }
}

/*
 * Declare unused parameters as "maybe_unused", since rxMessage is a callback
 * passed to libmctp and we have to match its expected prototype.
 */
void PCIeBinding::rxCTXMessage(uint8_t srcEid, [[maybe_unused]] void* data, void* msg,
               [[maybe_unused]] size_t len, [[maybe_unused]] void* binding_private)
{
    PCIeBinding* pThis = reinterpret_cast<PCIeBinding*>(data);
    struct mctp_binding* binding = mctp_nupcie_core(pThis->pcie);
    mctp_ctrl_msg_hdr* reqHeader = reinterpret_cast<mctp_ctrl_msg_hdr*>(msg);
	struct mctp_nupcie_pkt_private *pkt_prv =
        reinterpret_cast<struct mctp_nupcie_pkt_private *>(binding_private);
    uint8_t cmd = reqHeader->command_code;
    std::vector<uint8_t> resp = {};
    reqHeader->rq_dgram_inst &= static_cast<uint8_t>(~(MCTP_CTRL_HDR_FLAG_REQUEST));

    switch (cmd) {
	case MCTP_CTRL_CMD_PREPARE_ENDPOINT_DISCOVERY:
    {
        fprintf(stderr, "PREPARE_ENDPOINT_DISCOVERY\n");
        pThis->discoveredFlag = pcie_binding::DiscoveryFlags::Undiscovered;

        resp.resize(sizeof(mctp_ctrl_resp_prepare_discovery));
        mctp_ctrl_resp_prepare_discovery* respCmd =
            reinterpret_cast<mctp_ctrl_resp_prepare_discovery*>(resp.data());

	    memcpy(&respCmd->ctrl_hdr, reqHeader, sizeof(struct mctp_ctrl_msg_hdr));
        respCmd->completion_code = 0;
		pkt_prv->routing = PCIE_ROUTE_TO_RC;
		pkt_prv->remote_id = 0x00;
		pThis->DiscoveredFlag(pcie_binding::convertDiscoveryFlagsToString(pThis->discoveredFlag));
		break;
    }
	case MCTP_CTRL_CMD_ENDPOINT_DISCOVERY:
    {
        fprintf(stderr, "ENDPOINT_DISCOVERY\n");
		if (pThis->discoveredFlag == pcie_binding::DiscoveryFlags::Discovered) {
            fprintf(stderr, "Not handled becasue discovered %d \n", cmd);
			return;
		}
        resp.resize(sizeof(mctp_ctrl_resp_endpoint_discovery));
        mctp_ctrl_resp_endpoint_discovery* respCmd =
            reinterpret_cast<mctp_ctrl_resp_endpoint_discovery*>(resp.data());

	    memcpy(&respCmd->ctrl_hdr, reqHeader, sizeof(struct mctp_ctrl_msg_hdr));

		pkt_prv->routing = PCIE_ROUTE_TO_RC;
		pkt_prv->remote_id = 0x00;
		break;
    }
	case MCTP_CTRL_CMD_GET_ENDPOINT_ID:
    {
        fprintf(stderr, "GET_ENDPOINT_ID\n");
        resp.resize(sizeof(mctp_ctrl_resp_get_eid));
        mctp_ctrl_resp_get_eid* respCmd =
            reinterpret_cast<mctp_ctrl_resp_get_eid*>(resp.data());
	    memcpy(&respCmd->ctrl_hdr, reqHeader, sizeof(struct mctp_ctrl_msg_hdr));
	    mctp_ctrl_cmd_get_endpoint_id(binding->mctp, srcEid, false, respCmd);
		pkt_prv->routing = PCIE_ROUTE_BY_ID;

		break;
    }
	case MCTP_CTRL_CMD_SET_ENDPOINT_ID:
    {
        fprintf(stderr, "SET_ENDPOINT_ID\n");
		if (pThis->discoveredFlag == pcie_binding::DiscoveryFlags::Discovered) {
            fprintf(stderr, "Not handled becasue  discovered %d", cmd);
			return;
		}

        resp.resize(sizeof(mctp_ctrl_resp_set_eid));
        mctp_ctrl_resp_set_eid* respCmd =
            reinterpret_cast<mctp_ctrl_resp_set_eid*>(resp.data());
        memcpy(&respCmd->ctrl_hdr, reqHeader, sizeof(struct mctp_ctrl_msg_hdr));
        mctp_ctrl_cmd_set_eid* req = reinterpret_cast<mctp_ctrl_cmd_set_eid*>(msg);

        mctp_ctrl_cmd_set_endpoint_id(binding->mctp, srcEid,
					   req, respCmd);

		pThis->discoveredFlag = pcie_binding::DiscoveryFlags::Discovered;
		pkt_prv->routing = PCIE_ROUTE_BY_ID;
		pThis->DiscoveredFlag(pcie_binding::convertDiscoveryFlagsToString(pThis->discoveredFlag));
		pThis->Eid(req->eid);
		pThis->bdf = pkt_prv->own_id;
		pThis->Bdf(pThis->bdf);
		break;
    }
	default:
		fprintf(stderr, "Not handled %d", cmd);
		return;
	}

#ifdef MCTP_ASTPCIE_RESPONSE_WA
	pkt_prv->flags_seq_tag &= static_cast<uint8_t>(~(MCTP_HDR_FLAG_TO));
#endif

    mctp_message_tx(binding->mctp, srcEid, resp.data(), resp.size(), static_cast<void*>(pkt_prv));

}

PCIeBinding::~PCIeBinding()
{
    if (streamMonitor.native_handle() >= 0)
    {
        streamMonitor.release();
    }
    if (pcie)
    {
        mctp_nupcie_free(pcie);
    }
}
