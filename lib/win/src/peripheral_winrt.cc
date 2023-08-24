#include "peripheral_winrt.h"
#include "winrt_cpp.h"

#include <iostream>

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt::Windows::Storage::Streams;

using winrt::Windows::Devices::Bluetooth::BluetoothCacheMode;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristicsResult;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattDescriptorsResult;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattDeviceServicesResult;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;

// #define LOGE(message, ...) printf(__FUNCTION__ ": " message "\n", __VA_ARGS__)
extern std::string loggingPath;
extern void logMessage(const std::string &func, const std::string &message);

#define LOGE(message, ...)                                      \
    {                                                           \
        char buffer[1024];                                      \
        snprintf(buffer, sizeof(buffer), message, __VA_ARGS__); \
        logMessage(__FUNCTION__, buffer);                       \
    }

PeripheralWinrt::PeripheralWinrt(uint64_t bluetoothAddress,
                                 BluetoothLEAdvertisementType advertismentType, const int rssiValue,
                                 const BluetoothLEAdvertisement &advertisment)
{
    this->bluetoothAddress = bluetoothAddress;
    address = formatBluetoothAddress(bluetoothAddress);
    // Random addresses have the two most-significant bits set of the 48-bit address.
    addressType = (bluetoothAddress >= 211106232532992) ? RANDOM : PUBLIC;
    Update(rssiValue, advertisment, advertismentType);
}

PeripheralWinrt::~PeripheralWinrt()
{
    if (device.has_value() && connectionToken)
    {
        device->ConnectionStatusChanged(connectionToken);
    }
}

void PeripheralWinrt::Update(const int rssiValue, const BluetoothLEAdvertisement &advertisment,
                             const BluetoothLEAdvertisementType &advertismentType)
{
    std::string localName = ws2s(advertisment.LocalName().c_str());
    if (!localName.empty())
    {
        name = localName;
    }

    connectable = advertismentType == BluetoothLEAdvertisementType::ConnectableUndirected ||
                  advertismentType == BluetoothLEAdvertisementType::ConnectableDirected;

    manufacturerData.clear();

    for (auto ds : advertisment.DataSections())
    {
        if (ds.DataType() == BluetoothLEAdvertisementDataTypes::TxPowerLevel())
        {
            auto d = ds.Data();
            auto dr = DataReader::FromBuffer(d);
            txPowerLevel = dr.ReadByte();
            if (txPowerLevel >= 128)
                txPowerLevel -= 256;
            dr.Close();
        }
        if (ds.DataType() == BluetoothLEAdvertisementDataTypes::ManufacturerSpecificData())
        {
            auto d = ds.Data();
            auto dr = DataReader::FromBuffer(d);
            manufacturerData.resize(d.Length());
            dr.ReadBytes(manufacturerData);
            dr.Close();
        }
    }

    serviceUuids.clear();
    for (auto uuid : advertisment.ServiceUuids())
    {
        serviceUuids.push_back(toStr(uuid));
    }

    rssi = rssiValue;
}

void PeripheralWinrt::Disconnect()
{
    LOGE("Disconnecting device [%s] [%s], closing %zd services", address.c_str(), name.c_str(), cachedServices.size());
    for (auto &[key, value] : cachedServices)
    {
        try
        {
            LOGE("Closing service [%s] [%s]: [%s]", address.c_str(), name.c_str(), toStr(value.service.Uuid()).c_str());
            value.service.Close();
        }
        catch (...)
        {
            LOGE("Disconnect closing service failed");
        }
    }

    cachedServices.clear();

    if (device.has_value() && connectionToken)
    {
        device->ConnectionStatusChanged(connectionToken);
    }

    try
    {
        if (device.has_value())
        {
            device->Close();
        }
    }
    catch (...)
    {
        LOGE("Closing device failed");
    }

    device = std::nullopt;
    LOGE("Disconnect complete [%s] [%s].", address.c_str(), name.c_str());
}

void PeripheralWinrt::GetServiceFromDevice(
    winrt::guid serviceUuid, std::function<void(std::optional<GattDeviceService>)> callback)
{
    LOGE("[%s]", toStr(serviceUuid).c_str());
    if (device.has_value())
    {
        device->GetGattServicesForUuidAsync(serviceUuid, BluetoothCacheMode::Cached)
            .Completed([=](IAsyncOperation<GattDeviceServicesResult> result, auto &status)
                       {
                if (status == AsyncStatus::Completed)
                {
                    auto services = result.GetResults();
                    auto service = services.Services().First();
                    if (service.HasCurrent())
                    {
                        GattDeviceService s = service.Current();
                        cachedServices.insert(std::make_pair(serviceUuid, CachedService(s)));
                        LOGE("Successfully retrieved service [%s] from device [%s] [%s]", toStr(serviceUuid).c_str(), address.c_str(), name.c_str());
                        callback(s);
                    }
                    else
                    {
                        LOGE("GetGattServicesForUuidAsync: no service with given id");
                        callback(std::nullopt);
                    }
                }
                else
                {
                    LOGE("GetGattServicesForUuidAsync: failed with status: %d\n", status);
                    callback(std::nullopt);
                } });
    }
    else
    {
        LOGE("GetGattServicesForUuidAsync: no device currently connected");
        callback(std::nullopt);
    }
}

void PeripheralWinrt::GetService(winrt::guid serviceUuid,
                                 std::function<void(std::optional<GattDeviceService>)> callback)
{
    LOGE("[%s]", toStr(serviceUuid).c_str());
    auto it = cachedServices.find(serviceUuid);
    if (it != cachedServices.end())
    {
        LOGE("Found in cached service [%s]", toStr(serviceUuid).c_str());
        callback(it->second.service);
    }
    else
    {
        GetServiceFromDevice(serviceUuid, callback);
    }
}

void PeripheralWinrt::GetCharacteristicFromService(
    GattDeviceService service, winrt::guid characteristicUuid,
    std::function<void(std::optional<GattCharacteristic>)> callback)
{
    LOGE("serviceUuid: [%s], characteristicUuid: [%s]", toStr(service.Uuid()).c_str(), toStr(characteristicUuid).c_str());
    service.GetCharacteristicsForUuidAsync(characteristicUuid, BluetoothCacheMode::Cached)
        .Completed([=](IAsyncOperation<GattCharacteristicsResult> result, auto &status)
                   {
            if (status == AsyncStatus::Completed)
            {
                auto characteristics = result.GetResults();
                auto characteristic = characteristics.Characteristics().First();
                if (characteristic.HasCurrent())
                {
                    winrt::guid serviceUuid = service.Uuid();
                    CachedService& cachedService = cachedServices[serviceUuid];
                    GattCharacteristic c = characteristic.Current();
                    cachedService.characterisitics.insert(
                        std::make_pair(c.Uuid(), CachedCharacteristic(c)));
                    callback(c);
                }
                else
                {
                    LOGE("GetCharacteristicsForUuidAsync: no characteristic with given id");
                    callback(std::nullopt);
                }
            }
            else
            {
                LOGE("GetCharacteristicsForUuidAsync: failed with status: %d\n", status);
                callback(std::nullopt);
            } });

    LOGE("Complete serviceUuid: [%s], characteristicUuid: [%s]", toStr(service.Uuid()).c_str(), toStr(characteristicUuid).c_str());
}

void PeripheralWinrt::GetCharacteristic(
    winrt::guid serviceUuid, winrt::guid characteristicUuid,
    std::function<void(std::optional<GattCharacteristic>)> callback)
{
    LOGE("serviceUuid: [%s], characteristicUuid: [%s]", toStr(serviceUuid).c_str(), toStr(characteristicUuid).c_str());
    auto it = cachedServices.find(serviceUuid);
    if (it != cachedServices.end())
    {
        auto &cachedService = it->second;
        auto cit = cachedService.characterisitics.find(characteristicUuid);
        if (cit != cachedService.characterisitics.end())
        {
            callback(cit->second.characteristic);
        }
        else
        {
            GetCharacteristicFromService(cachedService.service, characteristicUuid, callback);
        }
    }
    else
    {
        GetServiceFromDevice(serviceUuid, [=](std::optional<GattDeviceService> service)
                             {
            if (service)
            {
                GetCharacteristicFromService(*service, characteristicUuid, callback);
            }
            else
            {
                LOGE("GetCharacteristic: get service failed");
                callback(nullptr);
            } });
    }
    LOGE("Complete serviceUuid: [%s], characteristicUuid: [%s]", toStr(serviceUuid).c_str(), toStr(characteristicUuid).c_str());
}

void PeripheralWinrt::GetDescriptorFromCharacteristic(
    GattCharacteristic characteristic, winrt::guid descriptorUuid,
    std::function<void(std::optional<GattDescriptor>)> callback)
{
    LOGE("");
    characteristic.GetDescriptorsForUuidAsync(descriptorUuid, BluetoothCacheMode::Cached)
        .Completed([=](IAsyncOperation<GattDescriptorsResult> result, auto &status)
                   {
            if (status == AsyncStatus::Completed)
            {
                auto descriptors = result.GetResults();
                auto descriptor = descriptors.Descriptors().First();
                if (descriptor.HasCurrent())
                {
                    GattDescriptor d = descriptor.Current();
                    winrt::guid characteristicUuid = characteristic.Uuid();
                    winrt::guid descriptorUuid = d.Uuid();
                    winrt::guid serviceUuid = characteristic.Service().Uuid();
                    CachedService& cachedService = cachedServices[serviceUuid];
                    CachedCharacteristic& c = cachedService.characterisitics[characteristicUuid];
                    c.descriptors.insert(std::make_pair(descriptorUuid, d));
                    callback(d);
                }
                else
                {
                    LOGE("GetDescriptorsForUuidAsync: no characteristic with given id");
                    callback(std::nullopt);
                }
            }
            else
            {
                LOGE("GetDescriptorsForUuidAsync: failed with status: %d\n", status);
                callback(std::nullopt);
            } });
    LOGE("Complete");
}

void PeripheralWinrt::GetDescriptor(winrt::guid serviceUuid, winrt::guid characteristicUuid,
                                    winrt::guid descriptorUuid,
                                    std::function<void(std::optional<GattDescriptor>)> callback)
{
    LOGE("");
    auto it = cachedServices.find(serviceUuid);
    if (it != cachedServices.end())
    {
        auto &cachedService = it->second;
        auto cit = cachedService.characterisitics.find(characteristicUuid);
        if (cit != cachedService.characterisitics.end())
        {
            GetDescriptorFromCharacteristic(cit->second.characteristic, descriptorUuid, callback);
        }
        else
        {
            GetCharacteristicFromService(
                cachedService.service, characteristicUuid,
                [=](std::optional<GattCharacteristic> characteristic)
                {
                    if (characteristic)
                    {
                        GetDescriptorFromCharacteristic(*characteristic, descriptorUuid, callback);
                    }
                    else
                    {
                        LOGE("GetDescriptor: get characteristic failed 1");
                        callback(nullptr);
                    }
                });
        }
    }
    else
    {
        GetServiceFromDevice(serviceUuid, [=](std::optional<GattDeviceService> service)
                             {
            if (service)
            {
                GetCharacteristicFromService(
                    *service, characteristicUuid,
                    [=](std::optional<GattCharacteristic> characteristic) {
                        if (characteristic)
                        {
                            GetDescriptorFromCharacteristic(*characteristic, descriptorUuid,
                                                            callback);
                        }
                        else
                        {
                            LOGE("GetDescriptor: get characteristic failed 2");
                            callback(nullptr);
                        }
                    });
            }
            else
            {
                LOGE("GetDescriptor: get service failed");
                callback(nullptr);
            } });
    }
    LOGE("Complete");
}
