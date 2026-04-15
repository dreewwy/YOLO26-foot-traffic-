#!/bin/bash
# Exit immediately if any command fails
set -e 

echo "=========================================="
echo " Starting Tracker Node Deployment..."
echo "=========================================="

# 1. System Dependencies (Added python3-venv)
echo "[1/4] Installing System Dependencies (OpenCV, CMake, Compilers)..."
sudo apt update
sudo apt install -y build-essential cmake git libopencv-dev python3-pip python3-venv cron wget

# 2. OpenVINO Installation
echo "[2/4] Installing OpenVINO 2024.1..."
if [ ! -d "$HOME/openvino_toolkit" ]; then
    wget https://storage.openvinotoolkit.org/repositories/openvino/packages/2024.1/linux/l_openvino_toolkit_ubuntu22_2024.1.0.15008.f4afc983258_x86_64.tgz -O /tmp/openvino.tgz
    mkdir -p ~/openvino_toolkit
    tar -xf /tmp/openvino.tgz --strip-components=1 -C ~/openvino_toolkit
    sudo ~/openvino_toolkit/install_dependencies/install_openvino_dependencies.sh
    rm /tmp/openvino.tgz
else
    echo "OpenVINO already installed, skipping download."
fi

# Make sure OpenVINO is loaded into the terminal profile
if ! grep -q "setupvars.sh" ~/.bashrc; then
    echo "source ~/openvino_toolkit/setupvars.sh" >> ~/.bashrc
fi

# Source it right now for the build step
source ~/openvino_toolkit/setupvars.sh

# 3. Python Telemetry Dependencies (USING VIRTUAL ENVIRONMENT)
echo "[3/4] Setting up Python Virtual Environment..."
# Create a virtual environment named '.venv' in the current directory
python3 -m venv .venv
# Activate it, install the packages, then deactivate it
source .venv/bin/activate
pip install msal requests
deactivate

# 4. Build the C++ Tracker
echo "[4/4] Building the C++ Tracker..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "=========================================="
echo " Deployment Complete! "
echo "=========================================="
echo "To start the tracker, run:"
echo "source ~/.bashrc && cd build && ./people_counter"
