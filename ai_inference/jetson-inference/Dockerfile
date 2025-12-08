# Custom Dockerfile for BB-8 AI Inference
# Extends the pre-built jetson-inference image to add websockets and SSL setup

FROM dustynv/jetson-inference:r32.7.1

# 1. Install websockets
RUN pip3 install websockets

# 2. Create startup script that persists env for exec sessions
RUN cat <<'EOF' > /start.sh && chmod +x /start.sh
#!/bin/bash
set -e

# Detect IP (prefer wlan0, fallback to eth0, else localhost)
IP=$(ip -4 addr show wlan0 2>/dev/null | grep -oP '(?<=inet )[^/]*' | head -n 1)
if [ -z "$IP" ]; then
    IP=$(ip -4 addr show eth0 2>/dev/null | grep -oP '(?<=inet )[^/]*' | head -n 1)
fi
if [ -z "$IP" ]; then
    IP="127.0.0.1"
fi
echo "Detected Jetson IP: $IP"

# Export for current session and persist for future shells
export JETSON_IP="$IP"
grep -q '^JETSON_IP=' /etc/environment && sed -i '/^JETSON_IP=/d' /etc/environment
echo "JETSON_IP=$IP" >> /etc/environment

# Generate SSL certificates if missing
if [ ! -f /jetson-inference/data/key.pem ] || [ ! -f /jetson-inference/data/cert.pem ]; then
    echo "Generating SSL certificates..."
    openssl req -x509 -newkey rsa:4096 \
        -keyout /jetson-inference/data/key.pem \
        -out /jetson-inference/data/cert.pem \
        -days 365 -nodes -subj "/CN=localhost"
fi

export SSL_KEY=/jetson-inference/data/key.pem
export SSL_CERT=/jetson-inference/data/cert.pem
grep -q '^SSL_KEY=' /etc/environment && sed -i '/^SSL_KEY=/d' /etc/environment
grep -q '^SSL_CERT=' /etc/environment && sed -i '/^SSL_CERT=/d' /etc/environment
echo "SSL_KEY=/jetson-inference/data/key.pem" >> /etc/environment
echo "SSL_CERT=/jetson-inference/data/cert.pem" >> /etc/environment

exec "$@"
EOF

ENTRYPOINT ["/start.sh"]
CMD ["/bin/bash"]




