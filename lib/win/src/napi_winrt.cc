//
//  napi_objc.mm
//  noble-mac-native
//
//  Created by Georg Vienna on 30.08.18.
//
#include "napi_winrt.h"

#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <rpc.h>

using namespace winrt::Windows::Devices::Bluetooth;

winrt::guid napiToUuid(Napi::String string)
{
    std::string str = string.Utf8Value();
    if (str.size() == 32)
    {
        str.insert(8, "-");
        str.insert(13, "-");
        str.insert(18, "-");
        str.insert(23, "-");
    }
    // Short ids arrive at two widths: 4 hex characters for a 16-bit id and 8 for a
    // 32-bit one. toStr emits both (see winrt_cpp.cc) and FromShortId is the exact
    // inverse of the TryGetShortId that produced them.
    if (str.size() == 4 || str.size() == 8)
    {
        try
        {
            // stoul, not stoi: an 8-character id above 0x7FFFFFFF does not fit in an
            // int, and std::stoi throws out_of_range on it.
            unsigned long id = std::stoul(str, nullptr, 16);
            return BluetoothUuidHelper::FromShortId(static_cast<uint32_t>(id));
        }
        catch (...)
        {
            // A non-hex string makes stoul throw. Every caller is an N-API entry
            // point and none of them catch, so return the null uuid rather than let
            // a C++ exception cross the boundary.
            return winrt::guid{};
        }
    }
    UUID uuid{};
    if (UuidFromString((RPC_CSTR)str.c_str(), &uuid) != RPC_S_OK)
    {
        // UuidFromString leaves uuid untouched when it rejects the string, so there
        // is nothing valid to read. The null uuid matches no service, characteristic
        // or descriptor, so the lookup fails cleanly instead of on a garbage value.
        return winrt::guid{};
    }
    std::array<uint8_t, 8> data4;
    std::copy_n(uuid.Data4, data4.size(), data4.begin());
    return winrt::guid(uuid.Data1, uuid.Data2, uuid.Data3, data4);
}

std::vector<winrt::guid> napiToUuidArray(Napi::Array array)
{
    std::vector<winrt::guid> uuids;
    for (size_t i = 0; i < array.Length(); i++)
    {
        Napi::Value val = array[i];
        uuids.push_back(napiToUuid(val.As<Napi::String>()));
    }
    return uuids;
}

Data napiToData(Napi::Buffer<byte> buffer)
{
    Data data;
    auto bytes = buffer.Data();
    data.assign(bytes, bytes + buffer.Length());
    return data;
}

int napiToNumber(Napi::Number number)
{
    return number.Int32Value();
}

std::vector<winrt::guid> getUuidArray(const Napi::Value& value)
{
    if (value.IsArray())
    {
        return napiToUuidArray(value.As<Napi::Array>());
    }
    return std::vector<winrt::guid>();
}

bool getBool(const Napi::Value& value, bool def)
{
    if (value.IsBoolean())
    {
        return value.As<Napi::Boolean>().Value();
    }
    return def;
}
