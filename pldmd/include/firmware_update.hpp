/**
 * Copyright © 2020 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sdbusplus/asio/object_server.hpp>
#include <vector>

namespace pldm
{
namespace fwu
{
using FWUVariantType =
    std::variant<uint8_t, uint16_t, uint32_t, uint64_t, std::string>;
using FWUProperties = std::map<std::string, FWUVariantType>;
using CompPropertiesMap = std::map<uint16_t, FWUProperties>;
using FDProperties = std::pair<FWUProperties, CompPropertiesMap>;

enum class DescriptorIdentifierType : uint16_t
{
    pciVendorID = 0,
    ianaEnterpriseID = 1,
    uuid = 2,
    pnpVendorID = 3,
    acpiVendorID = 4,
    pciDeviceID = 0x0100,
    pciSubsystemVendorID = 0x0101,
    pciSubsystemID = 0x0102,
    pciRevisionID = 0x0103,
    pnpProductIdentifier = 0x0104,
    acpiProductIdentifier = 0x0105
};

struct DescriptorHeader
{
    DescriptorIdentifierType type;
    uint16_t size;
};

class FWInventoryInfo
{
  public:
    FWInventoryInfo() = delete;
    FWInventoryInfo(const pldm_tid_t _tid);
    ~FWInventoryInfo();

    /** @brief runs inventory commands
     */
    std::optional<FDProperties>
        runInventoryCommands(boost::asio::yield_context yield);

  private:
    /** @brief run query device identifiers command
     * @return PLDM_SUCCESS on success and corresponding error completion code
     * on failure
     */
    int runQueryDeviceIdentifiers(boost::asio::yield_context yield);

    /** @brief run get firmware parameters command
     * @return PLDM_SUCCESS on success and corresponding error completion code
     * on failure
     */
    int runGetFirmwareParameters(boost::asio::yield_context yield);

    /** @brief API that unpacks get firmware parameters component data.
     */
    void unpackCompData(const uint16_t count,
                        const std::vector<uint8_t>& compData);

    /** @brief API that copies get firmware parameters component image set data
     * to fwuProperties map.
     */
    void copyCompImgSetData(
        const struct get_firmware_parameters_resp& respData,
        const struct variable_field& activeCompImgSetVerStr,
        const struct variable_field& pendingCompImgSetVerStr);

    /** @brief API that copies get firmware parameters component data to
     * fwuProperties map.
     */
    void copyCompData(const uint16_t count,
                      const struct component_parameter_table* componentData,
                      struct variable_field* activeCompVerStr,
                      struct variable_field* pendingCompVerStr);

    pldm_tid_t tid;
    const uint16_t timeout = 100;
    const size_t retryCount = 3;
    // map that holds the component properties of a terminus
    CompPropertiesMap compPropertiesMap;
};
} // namespace fwu
} // namespace pldm