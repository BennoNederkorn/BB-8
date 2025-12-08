# Custom Dockerfile for BB-8 AI Inference
# Extends the pre-built jetson-inference image to add websockets and SSL setup

FROM dustynv/jetson-inference:r32.7.1

# 1. Install websockets
RUN pip3 install websockets

# 2. Create entrypoint script
RUN echo '#!/bin/bash' > /usr/local/bin/docker-entrypoint.sh && \
    echo 'set -e' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '# 1. Save IP address' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'export JETSON_IP=$(ip -4 addr show wlan0 2>/dev/null | grep -oP "(?<=inet\s)\d+(\.\d+){3}" | head -n 1)' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'if [ -z "$JETSON_IP" ]; then' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '    export JETSON_IP=$(ip -4 addr show eth0 2>/dev/null | grep -oP "(?<=inet\s)\d+(\.\d+){3}" | head -n 1)' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'fi' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'echo "Detected JETSON_IP: $JETSON_IP"' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '# 2. Generate SSL Certs if missing' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'if [ ! -f /jetson-inference/data/key.pem ] || [ ! -f /jetson-inference/data/cert.pem ]; then' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '    echo "Generating SSL certificates..."' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '    openssl req -x509 -newkey rsa:4096 -keyout /jetson-inference/data/key.pem -out /jetson-inference/data/cert.pem -days 365 -nodes -subj "/CN=localhost" 2>/dev/null' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'fi' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'export SSL_KEY=/jetson-inference/data/key.pem' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'export SSL_CERT=/jetson-inference/data/cert.pem' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '' >> /usr/local/bin/docker-entrypoint.sh && \
    echo '# Execute the passed command' >> /usr/local/bin/docker-entrypoint.sh && \
    echo 'exec "$@"' >> /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["/bin/bash"]




