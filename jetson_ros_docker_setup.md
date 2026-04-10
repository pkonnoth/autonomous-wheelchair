# Jetson Orin NX — ROS2 Docker Setup Documentation

## Hardware & Software Stack

| Component | Details |
|---|---|
| Device | NVIDIA Jetson Orin NX |
| JetPack | 6.1 (L4T r36.4.0) |
| OS | Ubuntu 22.04 (ARM64) |
| ROS Version | ROS 2 Humble |
| Docker Image | `dustynv/ros:humble-desktop-l4t-r36.4.0` |
| Container Runtime | NVIDIA Container Toolkit |

---

## References & Links

- **NVIDIA Container Toolkit GitHub**: https://github.com/NVIDIA/nvidia-container-toolkit
- **NVIDIA Container Toolkit Docs**: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html
- **dusty-nv jetson-containers (official NVIDIA)**: https://github.com/dusty-nv/jetson-containers
- **Aurora ROS2 SDK**: https://github.com/Slamtec/aurora_ros/tree/ros2
- **Aurora ROS2 SDK Docs**: https://developer.slamtec.com/docs/slamware/aurora-ros2-sdk/slamware_ros_sdk_server_node/
- **JetsonHacks Docker Setup Guide**: https://jetsonhacks.com/2025/02/24/docker-setup-on-jetpack-6-jetson-orin/

---

## Docker Installation (JetPack 6 Host Flash)

> Docker is not installed by default on JetPack 6 host-based flashes. Use the jetsonhacks scripts.

```bash
git clone https://github.com/jetsonhacks/install-docker.git
cd install-docker
bash ./install_nvidia_docker.sh
bash ./configure_nvidia_docker.sh
```

---

## NVIDIA Container Runtime Configuration

The NVIDIA Container Toolkit was pre-installed via JetPack. If `--runtime nvidia` fails, fix the daemon config:

```bash
sudo tee /etc/docker/daemon.json <<EOF
{
    "runtimes": {
        "nvidia": {
            "path": "nvidia-container-runtime",
            "runtimeArgs": []
        }
    },
    "default-runtime": "nvidia"
}
EOF

sudo systemctl restart docker
```

Verify:
```bash
sudo docker info | grep -i runtime
# Expected: Runtimes: nvidia runc
# Expected: Default Runtime: nvidia
```

---

## Pulling the ROS2 Humble L4T Image

```bash
sudo docker pull dustynv/ros:humble-desktop-l4t-r36.4.0
```

> **Why this image?** It is the NVIDIA-recommended Jetson-optimised ROS2 image built on L4T (Linux for Tegra), with CUDA, cuDNN and TensorRT support baked in. It is maintained by Dustin Franklin, NVIDIA engineer, as part of the official `jetson-containers` project.

---

## Running the Container

```bash
sudo docker run --runtime nvidia -it --rm \
  --network=host \
  --volume ~/ros2_ws:/ros2_ws \
  dustynv/ros:humble-desktop-l4t-r36.4.0
```

| Flag | Purpose |
|---|---|
| `--runtime nvidia` | Enables GPU/CUDA access inside container |
| `--network=host` | Shares host network — required for ROS2 DDS discovery and Aurora IP connection |
| `--volume ~/ros2_ws:/ros2_ws` | Persists workspace across container restarts |

---

## First-Time Container Setup

Once inside the container:

```bash
# Source ROS2 (dustynv image uses install/ path)
source /opt/ros/humble/install/setup.bash
echo "source /opt/ros/humble/install/setup.bash" >> ~/.bashrc

# Verify ROS2
ros2 topic list
```

---

## Building the Aurora ROS2 SDK

```bash
# 1. Create workspace
mkdir -p ~/ros2_ws/src
cd ~/ros2_ws/src

# 2. Clone Aurora ROS2 SDK
git clone -b ros2 https://github.com/Slamtec/aurora_ros.git

# 3. Install dependencies
cd ~/ros2_ws
apt-get update
rosdep update
rosdep install --from-paths src --ignore-src -r -y

# 4. Build
colcon build

# 5. Source workspace
echo "source ~/ros2_ws/install/setup.bash" >> ~/.bashrc

# 6. Set ARM64 library path
echo "export LD_LIBRARY_PATH=~/ros2_ws/src/aurora_ros/src/aurora_remote_public/lib/linux_aarch64:\$LD_LIBRARY_PATH" >> ~/.bashrc

source ~/.bashrc
```

> **Note:** The Aurora SDK ships a pre-compiled `.so` library. Confirm the ARM64 lib exists:
> ```bash
> ls ~/ros2_ws/src/aurora_ros/src/aurora_remote_public/lib/
> ```

---

## Launching the Aurora SDK Node

Make sure the Aurora device is powered on and connected to the same network as your Jetson.

```bash
ros2 launch slamware_ros_sdk slamware_ros_sdk_server_and_view.xml ip_address:=<AURORA_IP>
```

Default Aurora AP mode IP: `192.168.11.1`

---

## Verify ROS2 is Working (Talker/Listener Test)

Open two terminals and attach to the same running container:

**Terminal 1:**
```bash
sudo docker exec -it <container_id> bash
source ~/.bashrc
ros2 run demo_nodes_cpp talker
```

**Terminal 2:**
```bash
sudo docker exec -it <container_id> bash
source ~/.bashrc
ros2 run demo_nodes_cpp listener
```

If you see `Publishing: Hello World: 1, 2, 3...` and `I heard: Hello World: 1, 2, 3...` — ROS2 is fully functional.

---

## Useful Docker Commands

```bash
# List all images
sudo docker images

# List running containers
sudo docker ps

# List all containers including stopped
sudo docker ps -a

# Re-enter a stopped container
sudo docker start <container_id>
sudo docker exec -it <container_id> bash

# Delete all stopped containers
sudo docker container prune
```
