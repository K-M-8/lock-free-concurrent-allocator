
import os
import pandas as pd
import streamlit as st
import matplotlib.pyplot as plt

st.set_page_config(
    page_title="Concurrent Allocator Dashboard",
    page_icon="⚙️",
    layout="wide"
)

st.title("High-Performance Concurrent Allocator")
st.caption(
    "Operating Systems Project | "
    "Mutex vs CAS-Based Memory Allocation"
)

CSV_FILE = "results.csv"

if not os.path.exists(CSV_FILE):
    st.error(
        "results.csv not found. "
        "Run the C++ benchmark first."
    )
    st.code(
        "g++ -std=c++17 -O2 -pthread main.cpp -o allocator\n"
        "./allocator"
    )
    st.stop()

df = pd.read_csv(CSV_FILE)

required = [
    "threads",
    "mutex_ops_sec",
    "cas_ops_sec",
    "mutex_avg_latency_ns",
    "cas_avg_latency_ns",
    "cas_retries",
    "peak_internal_waste_bytes",
]

missing = [c for c in required if c not in df.columns]

if missing:
    st.error(f"Missing CSV columns: {missing}")
    st.stop()

# Sidebar controls
st.sidebar.header("Dashboard Controls")

thread_options = sorted(df["threads"].unique())

selected_threads = st.sidebar.multiselect(
    "Thread counts",
    options=thread_options,
    default=thread_options
)

filtered = df[df["threads"].isin(selected_threads)]

if filtered.empty:
    st.warning("Select at least one thread count.")
    st.stop()

# Metric cards
latest = filtered.iloc[-1]

c1, c2, c3, c4 = st.columns(4)

c1.metric(
    "Threads in selected data",
    len(filtered)
)

c2.metric(
    "Highest mutex throughput",
    f"{filtered['mutex_ops_sec'].max():,.0f} ops/s"
)

c3.metric(
    "Highest CAS throughput",
    f"{filtered['cas_ops_sec'].max():,.0f} ops/s"
)

c4.metric(
    "Total CAS retries",
    f"{filtered['cas_retries'].sum():,}"
)

st.divider()

# Throughput chart
st.subheader("1. Throughput Comparison")

throughput = filtered.set_index("threads")[
    ["mutex_ops_sec", "cas_ops_sec"]
]

throughput.columns = [
    "Mutex allocator",
    "CAS allocator"
]

st.line_chart(throughput)

# Latency chart
st.subheader("2. Average Operation Latency")

latency = filtered.set_index("threads")[
    ["mutex_avg_latency_ns", "cas_avg_latency_ns"]
]

latency.columns = [
    "Mutex latency (ns)",
    "CAS latency (ns)"
]

st.line_chart(latency)

# CAS retry chart
st.subheader("3. CAS Retry Count")

retries = filtered.set_index("threads")[["cas_retries"]]
retries.columns = ["Failed CAS attempts"]

st.bar_chart(retries)

# Memory waste chart
st.subheader("4. Estimated Internal Memory Waste")

waste = filtered.set_index("threads")[
    ["peak_internal_waste_bytes"]
]

waste.columns = ["Estimated peak waste (bytes)"]

st.line_chart(waste)

# Throughput ratio
st.subheader("5. Relative Throughput")

ratio = (
    filtered["cas_ops_sec"]
    / filtered["mutex_ops_sec"].replace(0, float("nan"))
)

ratio_df = pd.DataFrame({
    "Threads": filtered["threads"].values,
    "CAS / Mutex throughput": ratio.values
}).set_index("Threads")

st.bar_chart(ratio_df)

st.caption(
    "A ratio above 1 indicates higher measured CAS throughput; "
    "below 1 indicates higher measured mutex throughput."
)

# Raw results
st.divider()
st.subheader("Benchmark Data")

st.dataframe(filtered, use_container_width=True)

st.download_button(
    "Download selected results as CSV",
    data=filtered.to_csv(index=False),
    file_name="allocator_results.csv",
    mime="text/csv"
)

st.info(
    "Results are measurements from your local benchmark. "
    "Performance depends on hardware, compiler settings, "
    "workload, and allocator design."
)