#!/bin/bash

# 清理网桥资源的脚本
BridgeName="br0"
TapName="tap0"
InterfaceName="eth0"
BridgeSeg="172.20.192.0/24"

echo "➤ 移除 TAP 接口 ${TapName}"
sudo ip tuntap del dev ${TapName} mode tap 2>/dev/null || echo "  └─ 接口不存在，已跳过"
echo "➤ 清除网桥 ${BridgeName} 的所有成员接口"
for iface in $(ip -o link show | awk -F': ' '{print $2}' | grep "@${BridgeName}"); do
    echo "  └─ 移除接口: ${iface}"
    sudo ip link set dev ${iface} nomaster
done
echo "➤ 删除网桥 ${BridgeName}"
{
    sudo ip link set dev ${BridgeName} down 2>/dev/null
    sudo ip link del dev ${BridgeName} type bridge 2>/dev/null 
} && echo "  └─ 成功删除网桥" || echo "  └─ 网桥不存在或删除失败"
echo "➤ 清理 NAT 规则"
sudo iptables -t nat -L POSTROUTING --line-numbers -n > /tmp/nat-rules.tmp
while read -r line; do
    rule_num=$(echo $line | awk '{print $1}')
    [ -z "$rule_num" ] && continue
    if echo "$line" | grep -q "MASQUERADE.*${BridgeSeg}.*${InterfaceName}"; then
        echo "  └─ 删除规则 #${rule_num}: $line"
        sudo iptables -t nat -D POSTROUTING ${rule_num}
    fi
done < /tmp/nat-rules.tmp
rm -f /tmp/nat-rules.tmp
echo "➤ 恢复 ${InterfaceName} 的原始状态"
sudo ip link set ${InterfaceName} up

echo "✅ 网桥资源清理完成"