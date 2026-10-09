# High-Performance Lock-Free Concurrent Allocator

A C++ Operating Systems project exploring concurrent memory allocation, atomic operations, Compare-And-Swap (CAS), synchronization, and multithreaded performance benchmarking.

## 📌 Project Overview

Traditional mutex-based memory allocation can experience thread contention when multiple threads allocate and deallocate memory concurrently.

This project implements an experimental fixed-size memory allocator using atomic operations and a CAS-based free-list. It compares the custom allocator with a mutex-protected `malloc/free` baseline and records performance metrics for different thread counts.

The benchmark results are exported to CSV and visualized using Python.

## 🎯 Objectives

* Understand mutex-based synchronization and lock-free coordination.
* Implement a CAS-based shared free-list.
* Support multiple memory size classes.
* Benchmark allocation and deallocation under multithreaded workloads.
* Measure throughput, average operation latency, CAS retries, and estimated internal fragmentation.
* Visualize benchmark results using Python and Streamlit.

## 🛠️ Technologies Used

* **C++17** — allocator implementation and benchmarking
* **GCC / MinGW-w64** — C++ compilation
* **C++ Standard Library** — threads, atomics, mutexes and timing
* **Python** — data analysis and visualization
* **Pandas** — CSV processing
* **Matplotlib** — benchmark graphs
* **Streamlit** — interactive dashboard
* **Git and GitHub** — version control

## 🏗️ Project Architecture

```text
Concurrent Allocator
        |
        v
Multithreaded Benchmark
        |
        +-------------------+
        |                   |
        v                   v
 Mutex Allocator      CAS Allocator
 malloc/free          Fixed-size pools
                            |
                            v
                      Atomic Free-list
                            |
                            v
                     Multiple Size Classes
        |
        v
     results.csv
        |
        +---------------------+
        |                     |
        v                     v
  Python Graphs        Streamlit Dashboard
```

## 📂 Project Structure

```text
LockFreeAllocator/
├── main.cpp
├── visualize.py
├── dashboard.py
├── README.md
├── .gitignore
├── results.csv          # Generated benchmark data
└── graphs/              # Generated graph images
```

## ⚙️ Requirements

* C++ compiler supporting C++17
* Python 3.9 or later recommended
* VS Code
* Git
* Internet access for installing Python packages

## 🚀 Installation and Execution

### 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/lock-free-concurrent-allocator.git
cd lock-free-concurrent-allocator
```

Replace `YOUR_USERNAME` with your GitHub username.

### 2. Compile the C++ program

**Windows:**

```powershell
g++ -std=c++17 -O2 -pthread main.cpp -o allocator.exe
```

**Linux:**

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o allocator
```

### 3. Run the benchmark

**Windows:**

```powershell
.\allocator.exe
```

**Linux:**

```bash
./allocator
```

The program tests workloads with 1, 2, 4 and 8 threads and generates `results.csv`.

### 4. Install Python dependencies

```bash
python -m pip install pandas matplotlib streamlit
```

On systems that use `python3`, replace `python` with `python3`.

### 5. Generate benchmark graphs

```bash
python visualize.py
```

The graphs are saved in the `graphs/` directory.

### 6. Launch the dashboard

```bash
python -m streamlit run dashboard.py
```

Open the local URL displayed in the terminal, usually `http://localhost:8501`.

## 📊 Performance Metrics

| Metric                 | Description                                                  |
| ---------------------- | ------------------------------------------------------------ |
| Throughput             | Number of benchmark workload operations completed per second |
| Average latency        | Average measured duration per workload operation             |
| CAS retries            | Failed CAS attempts recorded by the allocator                |
| Internal fragmentation | Estimated unused bytes within allocated size-class blocks    |
| Reserved capacity      | Configured payload capacity of the memory pools              |

Actual performance depends on processor architecture, compiler settings, operating system, thread count, and allocation patterns. No performance improvement is assumed without measurements.

## 🧠 Operating Systems Concepts

### Module 4: Synchronization and Lock-Free Coordination

* Mutex-based synchronization
* Multithreading
* Atomic variables
* Compare-And-Swap (CAS)
* Shared-data coordination
* Contention and retry behavior

### Module 5: Memory Allocation

* Dynamic memory allocation and deallocation
* Fixed-size memory pools
* Size classes
* Memory utilization
* Internal fragmentation
* Allocation performance measurement

## ⚠️ Limitations

* The allocator uses fixed-size memory pools and supports requests up to 512 bytes.
* Pool capacity is bounded; allocation can fail when a size class is exhausted.
* The current implementation is an educational prototype, not a replacement for the system allocator.
* Lock-free progress depends on platform atomic support and the implementation's correctness.
* Benchmark results are affected by measurement overhead and workload characteristics.
* Memory-waste figures are estimates rather than exact measurements of physical memory usage.

## 🔮 Future Enhancements

* Thread-local allocation caches with safe cross-thread deallocation.
* Improved allocation-failure handling and correctness tests.
* Repeated benchmark runs and latency percentile measurements.
* Cache-contention and cache-miss analysis using platform profiling tools.
* More detailed fragmentation and memory-utilization metrics.
* Interactive visualization of throughput, latency, and CAS retries.
* Comparison with additional production allocators.

## 🎓 Academic Project

**Project title:** High-Performance Lock-Free Concurrent Allocator & Visualizer

**Course:** Operating Systems

**Syllabus modules:** Module 4 — Synchronization and Lock-Free Coordination; Module 5 — Memory Allocation

**Team members:** Add your team members' names here.

**Project guide:** Add your guide's name here.

## 📜 License

This project is intended for educational and academic use. Add an appropriate open-source license if you intend to distribute or reuse it under specific terms.
