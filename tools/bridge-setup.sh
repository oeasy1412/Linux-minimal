#!/bin/bash
sudo apt-get install kmod bridge-utils uml-utilities
sudo modprobe tun bridge

BridgeIP=172.20.192.1/24
BridgeSeg=172.20.192.0/24
BridgeName="br0"
InterfaceName="eth0"
TapName="tap0"

sudo ip link del ${BridgeName} 2>/dev/null
sudo ip link del ${TapName} 2>/dev/null
# GATEWAY=cat /etc/resolv.conf | grep nameserver | awk '{print $2}'
# sudo ip route add default via $(GATEWAY)
sudo ip link add name ${BridgeName} type bridge  # 创建网桥
sudo ip addr add $BridgeIP dev ${BridgeName}
sudo ip link set ${BridgeName} up
sudo ip tuntap add dev ${TapName} mode tap user $(whoami)  # 创建 TAP 接口
sudo ip link set ${TapName} up
sudo ip link set ${TapName} master ${BridgeName}  # 将 TAP 加入网桥

# 添加 NAT 规则
sudo iptables -t nat -A POSTROUTING -s $BridgeSeg -o ${InterfaceName} -j MASQUERADE
sudo iptables -t nat -L POSTROUTING -v --line-numbers

# sudo ip link set eth0 master ${BridgeName}  # 替换 eth0 为实际网卡名
# sudo dhclient ${BridgeName}  # 为网桥获取 IP（或手动配置）