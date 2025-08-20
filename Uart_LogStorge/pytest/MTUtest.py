import sys
from bleak import BleakScanner, BleakClient

async def find_ble_device(device_name):
    # 扫描并查找指定名称的BLE设备
    devices = await BleakScanner.discover()
    for device in devices:
        if device.name and device_name.lower() in device.name.lower():
            return device.address
    return None

async def get_ble_mtu(device_address):
    # 连接设备并获取MTU
    async with BleakClient(device_address) as client:
        if client.is_connected:
            # 获取MTU（不同设备可能需要不同方式，这里使用通用属性）
            mtu = client.mtu_size
            return mtu
    return None

async def main():


    target_name = "ESP32C3_UARTLOGGER"

    print(f"正在搜索BLE设备: {target_name}...")
    device_address = await find_ble_device(target_name)

    if not device_address:
        print(f"未找到名称包含 '{target_name}' 的BLE设备")
        sys.exit(1)

    print(f"找到设备，地址: {device_address}，正在获取MTU...")
    mtu = await get_ble_mtu(device_address)

    if mtu:
        print(f"BLE设备 '{target_name}' 的MTU值为: {mtu}")
    else:
        print(f"无法获取设备 '{target_name}' 的MTU值")

if __name__ == "__main__":
    import asyncio
    asyncio.run(main())