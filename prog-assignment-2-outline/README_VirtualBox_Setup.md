# **Setting Up a Virtual Ubuntu Environment for SchedLab on VirtualBox**

### **Overview**

This guide will help you set up an **Ubuntu 20.04+ VM** on **VirtualBox**, whether your **host machine** is running on **Intel** (x86) or **ARM** (e.g., macOS with Apple Silicon). The steps below will cover the required dependencies for running the **SchedLab** experiment using **eBPF** on a fresh Ubuntu installation inside a VirtualBox VM.

---

## **Step 1: Install VirtualBox and Prepare Ubuntu 20.04+ VM**

Before setting up the environment, you need to install **VirtualBox** on your host machine (Intel or ARM-based) and install Ubuntu 20.04+ in the VM.

### 1.1 **Install VirtualBox**

1. **For Intel-based systems** (e.g., most Windows/Linux PCs):

   * Download and install VirtualBox from [here](https://www.virtualbox.org/).
   * Follow the installation instructions for **Windows**, **Linux**, or **macOS** as needed.

2. **For ARM-based systems** (e.g., Apple Silicon macOS):

   * **Install VirtualBox for macOS**:
     Download and install VirtualBox from [here](https://www.virtualbox.org/wiki/Downloads).
   * On ARM Macs, **make sure VirtualBox 6.1.18 or later** is installed as it offers better support for ARM-based systems.

### 1.2 **Download Ubuntu 20.04+ ISO**

Download the **Ubuntu 24.04 LTS** ISO from [here](https://ubuntu.com/download/server/arm (for ARM/Mac M1/M2) or https://ubuntu.com/download/server#manual-install (for Intel/AMD hosts)). Any **Ubuntu 20.04+ LTS** will also work.

### 1.3 **Create the Virtual Machine**

1. Open **VirtualBox** and click **New**.
2. Set the **name** as **Ubuntu** and the **type** to **Linux** and **version** to **Ubuntu (64-bit)**.
3. Assign **2GB RAM** (at least 4GB recommended if possible).
4. Create a **20GB or larger virtual hard disk** for the Ubuntu VM.

### 1.4 **Install Ubuntu in the VM**

1. Attach the **Ubuntu ISO** to the VM's **optical drive**.
2. Start the VM and follow the instructions to install Ubuntu.
3. Set up the **user account** and **password** during the installation process.

---

## **Step 2: Install Dependencies and Set Up the Environment**

Once the **Ubuntu VM** is running, follow the steps below to install the necessary software dependencies.

### 2.1 **Update the System**

Open a terminal in Ubuntu and update the system:

```bash
sudo apt update
sudo apt upgrade -y
```

### 2.2 **Install Essential Development Tools**

These packages are required to build eBPF programs and interact with kernel tracepoints.

```bash
sudo apt install -y build-essential clang llvm make pkg-config libbpf-dev libelf-dev gcc libpcap-dev iproute2 linux-headers-$(uname -r) bpfcc-tools bpftrace linux-tools-common linux-tools-$(uname -r)
```

#### **Explanation**:

* **clang** and **llvm**: For compiling eBPF programs.
* **make**: Essential build tool.
* **libelf-dev**: For working with ELF files (used by eBPF).
* **libpcap-dev**: For packet capture functionality (optional, but useful).
* **bpfcc-tools** and **bpftool**: Utilities for eBPF and kernel interaction.
* **linux-headers**: Kernel headers needed to compile eBPF programs.

### 2.3 **Install Stress-ng for Load Generation**

To simulate workloads, install **stress-ng**:

```bash
sudo apt install -y stress-ng
```

This will be used to generate workloads for the experiment.

### 2.4 **Install Python and Visualization Libraries (Optional)**

If you want to analyze and visualize the results (CSV files, plotting, etc.), they may need Python libraries such as **matplotlib** and **pandas**:

```bash
sudo apt install -y python3 python3-pip
pip3 install matplotlib pandas
```

Alternatively, they can create a **virtual environment** to isolate your Python environment:

```bash
python3 -m venv schedlab-env
source schedlab-env/bin/activate
pip install matplotlib pandas
```

---

## **Step 3: Set Up SchedLab and BPF Tools**


### 3.1 **Build the BPF Program (schedlab.bpf.c)**

Compile the **SchedLab eBPF program** using the `make` command:

```bash
make
```

This will compile the BPF program (`schedlab.bpf.o`) and the user-space code (`schedlab_user.c`).

### 3.2 **Verify the BPF Program Compilation**

Check if the eBPF program was compiled successfully:

```bash
ls -lh schedlab.bpf.o
```

If the file exists and has the expected size, the build was successful.

---

## **Step 4: Running the Experiment**

Once everything is set up, you can run the **SchedLab experiment** with **eBPF tracing**.

### 4.1 **Run the Experiment in Streaming Mode**

For real-time observation of the scheduler:

```bash
sudo ./schedlab --mode stream --csv --csv-header > results.csv
```

This command runs **SchedLab**, collects the scheduling data, and stores it in **results.csv**.

### 4.2 **Run the Experiment with Starvation Detection**

To track tasks that experience **long wait times** (starvation):

```bash
sudo ./schedlab --mode starvation --wait-alert-ms 20 --csv --csv-header > starvation_results.csv
```

This will save **starvation alerts** to **starvation\_results.csv**.

---

## **Step 5: Troubleshooting**

If you run into issues, here are a few **troubleshooting tips**:

1. **Permissions Error**:

   * If there are errors loading the BPF program, ensure they are using `sudo` since BPF programs require elevated privileges to interact with the kernel.

2. **Missing Kernel Headers**:

   * If the `make` command fails due to missing headers, ensure they have installed the correct **kernel headers** using:

     ```bash
     sudo apt install linux-headers-$(uname -r)
     ```

3. **Out of Memory**:

   * If VirtualBox reports memory issues, increase the VM's **RAM allocation** (e.g., set to **4 GB** or more).

4. **VirtualBox Network Issues (Optional)**:

   * If network tools are needed, you may need to configure the VM's network adapter to **NAT** or **Bridged Adapter** mode.

---

## **Step 6: Additional Notes**

* If you are using **Intel CPUs** in VirtualBox, ensure **hardware virtualization** is enabled in the **BIOS/UEFI** settings to improve performance.
* **ARM-based Macs** (e.g., Apple M1/M2) should use the **latest VirtualBox version** and ensure **Apple's Rosetta 2** is enabled for emulation.

---

### **Final Remarks**

By following these instructions, you will set up a **virtualized Ubuntu environment** and have all the required tools for the **SchedLab experiment** ready. They will then be able to explore and analyze **Linux scheduler behavior** with the power of **eBPF**.

Feel free to reach out if there are any more questions or additional setup steps needed!
## Optional: SSH Access with Password‑free Root Login
For easier remote access (for example from VS Code or your host terminal), you can set up SSH:

1. **Inside the VM**, install and enable OpenSSH server:

   ```bash
   sudo apt update
   sudo apt install -y openssh-server
   sudo systemctl enable --now ssh
   ```

2. **Set up key‑based, password‑free login for root (optional):**
   * On your host, create a key if you don’t already have one:
     ```bash
     ssh-keygen -t ed25519
     ```
   * Copy the public key to the VM (replace 2222 with the forwarded port if using NAT):
     ```bash
     ssh-copy-id -i ~/.ssh/id_ed25519.pub -p 2222 root@localhost
     ```
   * You can now SSH without a password:
     ```bash
     ssh -i ~/.ssh/id_ed25519 -p 2222 root@localhost
     ```

3. **Connecting from VS Code:**
   * Install the **Remote – SSH** extension.
   * Add a host entry pointing to `localhost` on port `2222` in your VS Code SSH configuration.
   * You can then open the VM filesystem directly in VS Code.

This step is optional; you can always use the VirtualBox console if you prefer.
