#include "MCTPBinding.hpp"

#include <systemd/sd-id128.h>

#include <phosphor-logging/log.hpp>

#include "libmctp.h"

constexpr sd_id128_t mctpdAppId = SD_ID128_MAKE(c4, e4, d9, 4a, 88, 43, 4d, f0,
                                                94, 9d, bb, 0a, af, 53, 4e, 6d);

MctpBinding::MctpBinding(std::shared_ptr<object_server>& objServer,
                         std::string& objPath, ConfigurationVariant& conf,
                         boost::asio::io_context& ioc) :
    io(ioc)
{
    std::shared_ptr<dbus_interface> mctpInterface =
        objServer->add_interface(objPath, mctp_server::interface);

    try
    {
        if (SMBusConfiguration* smbusConf =
                std::get_if<SMBusConfiguration>(&conf))
        {
            eid = smbusConf->defaultEid;
            bindingID = smbusConf->bindingType;
            bindingMediumID = smbusConf->mediumId;
            bindingModeType = smbusConf->mode;
        }
        else if (PcieConfiguration* pcieConf =
                     std::get_if<PcieConfiguration>(&conf))
        {
            eid = pcieConf->defaultEid;
            bindingID = pcieConf->bindingType;
            bindingMediumID = pcieConf->mediumId;
            bindingModeType = pcieConf->mode;
        }
        else
        {
            throw std::system_error(
                std::make_error_code(std::errc::invalid_argument));
        }

        createUuid();
        registerProperty(mctpInterface, "Eid", eid);

        registerProperty(mctpInterface, "StaticEid", staticEid);

        registerProperty(mctpInterface, "Uuid", uuid);

        registerProperty(mctpInterface, "BindingID",
                         mctp_server::convertBindingTypesToString(bindingID));

        registerProperty(
            mctpInterface, "BindingMediumID",
            mctp_server::convertMctpPhysicalMediumIdentifiersToString(
                bindingMediumID));

        registerProperty(
            mctpInterface, "BindingMode",
            mctp_server::convertBindingModeTypesToString(bindingModeType));

        if (mctpInterface->initialize() == false)
        {
            throw std::system_error(
                std::make_error_code(std::errc::function_not_supported));
        }
    }
    catch (std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "MCTP Interface initialization failed.",
            phosphor::logging::entry("Exception:", e.what()));
        throw;
    }
}

void MctpBinding::createUuid(void)
{
    sd_id128_t id;

    if (sd_id128_get_machine_app_specific(mctpdAppId, &id))
    {
        throw std::system_error(
            std::make_error_code(std::errc::address_not_available));
    }

    uuid.insert(uuid.begin(), std::begin(id.bytes), std::end(id.bytes));
    if (uuid.size() != 16)
    {
        throw std::system_error(std::make_error_code(std::errc::bad_address));
    }
}
