sudo sysctl -w net.core.wmem_default=104857600
sudo sysctl -w net.core.wmem_max=209715200
sudo sysctl -w net.ipv4.tcp_wmem='10485760 104857600 209715200' # 10M 100M 200M
sudo sysctl -w net.core.rmem_default=104857600
sudo sysctl -w net.core.rmem_max=209715200
sudo sysctl -w net.ipv4.tcp_rmem='10485760 104857600 209715200'
