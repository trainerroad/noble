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

#define LOGE(message, ...) printf(__FUNCTION__ ": " message "\n", __VA_ARGS__)

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
    LOGE("++ PeripheralWinrt Update");
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
    LOGE("++ PeripheralWinrt Update return");
}

void PeripheralWinrt::Disconnect()
{
    LOGE("++ PeripheralWinrt Disconnect");
    for (const auto &[key, value] : cachedServices)
    {
        try
        {
            LOGE("++ PeripheralWinrt CLOSING SERVICE");
            value.service.Close();
        }
        catch (...)
        {
            std::cout << "Disconnect closing service failed" << std::endl;
        }
    }

    LOGE("++ PeripheralWinrt Disconnect 1");

    cachedServices.clear();

    LOGE("++ PeripheralWinrt Disconnect 2 ");

    if (device.has_value() && connectionToken)
    {
        device->ConnectionStatusChanged(connectionToken);
    }

    LOGE("++ PeripheralWinrt Disconnect 3");

    try
    {
        if (device.has_value())
        {
            device->Close();
        }
    }
    catch (...)
    {
        std::cout << "Disconnect closing device failed" << std::endl;
    }

    device = std::nullopt;
    LOGE("++ PeripheralWinrt Disconnect return");
}

void PeripheralWinrt::GetServiceFromDevice(
    winrt::guid serviceUuid, std::function<void(std::optional<GattDeviceService>)> callback)
{
    LOGE("++ PeripheralWinrt GetServiceFromDevice");
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
                        callback(s);
                    }
                    else
                    {
                        printf("GetGattServicesForUuidAsync: no service with given id\n");
                        callback(std::nullopt);
                    }
                }
                else
                {
                    printf("GetGattServicesForUuidAsync: failed with status: %d\n", status);
                    callback(std::nullopt);
                } });
    }
    else
    {
        printf("GetGattServicesForUuidAsync: no device currently connected\n");
        callback(std::nullopt);
    }

    LOGE("++ PeripheralWinrt GetServiceFromDevice return");
}

void PeripheralWinrt::GetService(winrt::guid serviceUuid,
                                 std::function<void(std::optional<GattDeviceService>)> callback)
{
    LOGE("++ PeripheralWinrt GetService");
    auto it = cachedServices.find(serviceUuid);
    if (it != cachedServices.end())
    {
        callback(it->second.service);
    }
    else
    {
        GetServiceFromDevice(serviceUuid, callback);
    }
    LOGE("++ PeripheralWinrt GetService return");
}

void PeripheralWinrt::GetCharacteristicFromService(
    GattDeviceService service, winrt::guid characteristicUuid,
    std::function<void(std::optional<GattCharacteristic>)> callback)
{
    LOGE("++ PeripheralWinrt GetCharacteristicFromService");
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
                    printf("GetCharacteristicsForUuidAsync: no characteristic with given id\n");
                    callback(std::nullopt);
                }
            }
            else
            {
                printf("GetCharacteristicsForUuidAsync: failed with status: %d\n", status);
                callback(std::nullopt);
            } });

    LOGE("++ PeripheralWinrt GetCharacteristicFromService return");
}

void PeripheralWinrt::GetCharacteristic(
    winrt::guid serviceUuid, winrt::guid characteristicUuid,
    std::function<void(std::optional<GattCharacteristic>)> callback)
{
    LOGE("++ PeripheralWinrt GetCharacteristic");
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
                printf("GetCharacteristic: get service failed\n");
                callback(nullptr);
            } });
    }
    LOGE("++ PeripheralWinrt GetCharacteristic return");
}

void PeripheralWinrt::GetDescriptorFromCharacteristic(
    GattCharacteristic characteristic, winrt::guid descriptorUuid,
    std::function<void(std::optional<GattDescriptor>)> callback)
{
    LOGE("++ PeripheralWinrt GetDescriptorFromCharacteristic");
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
                    printf("GetDescriptorsForUuidAsync: no characteristic with given id\n");
                    callback(std::nullopt);
                }
            }
            else
            {
                printf("GetDescriptorsForUuidAsync: failed with status: %d\n", status);
                callback(std::nullopt);
            } });
    LOGE("++ PeripheralWinrt GetDescriptorFromCharacteristic return");
}

void PeripheralWinrt::GetDescriptor(winrt::guid serviceUuid, winrt::guid characteristicUuid,
                                    winrt::guid descriptorUuid,
                                    std::function<void(std::optional<GattDescriptor>)> callback)
{
    LOGE("++ PeripheralWinrt GetDescriptor");
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
                        printf("GetDescriptor: get characteristic failed 1\n");
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
                            printf("GetDescriptor: get characteristic failed 2\n");
                            callback(nullptr);
                        }
                    });
            }
            else
            {
                printf("GetDescriptor: get service failed\n");
                callback(nullptr);
            } });
    }
    LOGE("++ PeripheralWinrt GetDescriptor return");
}
