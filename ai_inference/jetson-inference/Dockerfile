# Custom Dockerfile for BB-8 AI Inference
# Extends the pre-built jetson-inference image to add websockets and SSL setup

FROM dustynv/jetson-inference:r32.7.1

# 1. Install websockets
RUN pip3 install websockets

# 2. Install onnxruntime (GPU version for Jetson)
RUN wget https://nvidia.box.com/shared/static/pmsqsiaw4pg9qrbeckcbymho6c01jj4z.whl -O onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    pip3 install onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    rm onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl

# 3. Create startup script (SSL setup only; no IP export)
RUN printf '%s\n' \
  '#!/bin/bash' \
  'set -e' \
  '' \
  '# Generate SSL certificates if missing' \
  'if [ ! -f /jetson-inference/data/key.pem ] || [ ! -f /jetson-inference/data/cert.pem ]; then' \
  '    echo "Generating SSL certificates..."' \
  '    openssl req -x509 -newkey rsa:4096 \\' \
  '        -keyout /jetson-inference/data/key.pem \\' \
  '        -out /jetson-inference/data/cert.pem \\' \
  '        -days 365 -nodes -subj "/CN=localhost"' \
  'fi' \
  '' \
  'export SSL_KEY=/jetson-inference/data/key.pem' \
  'export SSL_CERT=/jetson-inference/data/cert.pem' \
  'echo "SSL_KEY=/jetson-inference/data/key.pem" >> /etc/environment' \
  'echo "SSL_CERT=/jetson-inference/data/cert.pem" >> /etc/environment' \
  '' \
  'exec "$@"' \
  > /start.sh \
  && chmod +x /start.sh
ENTRYPOINT ["/start.sh"]
CMD ["/bin/bash"]




