#!/bin/bash

# 下载 esp_websocket_client 组件到项目中
mkdir -p components/esp_websocket_client
cd components/esp_websocket_client

# 下载必要的文件
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/include/esp_websocket_client.h -o include/esp_websocket_client.h
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/include/esp_websocket_private.h -o include/esp_websocket_private.h
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/include/transport_ws.h -o include/transport_ws.h

mkdir -p include/private
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/include/private/esp_websocket_client_internal.h -o include/private/esp_websocket_client_internal.h

mkdir -p src
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/src/esp_websocket_client.c -o src/esp_websocket_client.c
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/src/transport_ws.c -o src/transport_ws.c

mkdir -p port
curl -sL https://raw.githubusercontent.com/espressif/esp-protocols/master/components/esp_websocket_client/port/esp_transport_ws.c -o port/esp_transport_ws.c

# 创建 CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
idf_component_register(SRCS "src/esp_websocket_client.c" "src/transport_ws.c" "port/esp_transport_ws.c"
                    INCLUDE_DIRS "include" "include/private" "port"
                    REQUIRES esp_http_client mbedtls)
EOF

echo "esp_websocket_client 组件下载完成！"
