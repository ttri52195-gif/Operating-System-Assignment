# OSLabs: Operating System Kernel Simulation 🖥️


> **Mô phỏng nhân hệ điều hành (Kernel Simulation)** tập trung vào Quản lý bộ nhớ ảo (Virtual Memory) theo kiến trúc 64-bit và Lập lịch tiến trình (Process Scheduling). Dự án mô phỏng các tương tác phần cứng-phần mềm trong không gian người dùng (User-space) sử dụng `pthreads`.

## 📖 Mục lục
1. [Giới thiệu](#-giới-thiệu)
3. [Kiến trúc hệ thống](#-kiến-trúc-hệ-thống)
4. [Cấu trúc mã nguồn](#-cấu-trúc-mã-nguồn)
5. [Cấu hình hệ thống](#-cấu-hình-hệ-thống)
6. [Hướng dẫn Build và Run](#-hướng-dẫn-build-và-run)
7. [Debugging & Logs](#-debugging--logs)

---

## 📖 Giới thiệu

* **CPU:** Sử dụng các luồng (Threads) để chạy song song các tiến trình.
* **RAM (Physical Memory):** Mảng byte lớn mô phỏng không gian địa chỉ vật lý.
* **Backing Store (Swap):** Thiết bị lưu trữ phụ để mở rộng bộ nhớ ảo.

Mục tiêu chính là hiện thực hóa cơ chế **Address Translation** (Dịch địa chỉ ảo sang vật lý) và xử lý **Page Fault** phức tạp.

---

## ⚙️ Tính năng kỹ thuật

### 1. Quản lý bộ nhớ (Advanced Memory Management)
* **Hierarchical Paging (64-bit):** Mô phỏng bảng trang 5 cấp độ (PGD $\rightarrow$ P4D $\rightarrow$ PUD $\rightarrow$ PMD $\rightarrow$ PTE) thay vì 2 cấp truyền thống.
* **TLB (Translation Lookaside Buffer):**
    * Tích hợp bộ nhớ đệm phần mềm cho các bản dịch địa chỉ.
    * Chiến lược: **LRU (Least Recently Used)** approximation (đưa entry vừa truy cập lên đầu).
    * Hỗ trợ thống kê **Hit/Miss Rate**.
* **Swapping & Page Replacement:**
    * Tự động phát hiện khi RAM đầy.
    * Chiến lược chọn nạn nhân: **Global FIFO** (First-In, First-Out).
    * Cơ chế **Swap Out** (RAM $\rightarrow$ Disk) và **Swap In** (Disk $\rightarrow$ RAM) trong suốt với người dùng.

### 2. Lập lịch (Scheduler)
* **Multi-Level Queue (MLQ):** Hàng đợi đa mức ưu tiên.
* **Slot-based Mechanism:** Mỗi mức ưu tiên có số lượng "time slot" khác nhau để đảm bảo công bằng và tránh "đói" tài nguyên (starvation).

### 3. Đồng bộ hóa (Concurrency)
* Thread-safe kernel đảm bảo an toàn dữ liệu khi nhiều CPU giả lập cùng truy cập RAM hoặc cập nhật bảng trang thông qua `pthread_mutex`.

---

## 🏗️ Kiến trúc hệ thống



### Luồng xử lý truy cập bộ nhớ (Memory Access Flow)
Khi một tiến trình (Process) cần đọc/ghi vào một địa chỉ ảo, Kernel thực hiện quy trình sau (trong `libmem.c`):

1.  **Check TLB:** Tìm trong bộ đệm TLB.
    * *Hit:* Lấy Frame Number (FPN) ngay lập tức.
    * *Miss:* Tiếp tục bước 2.
2.  **Hardware Page Walk:** Duyệt qua 5 cấp bảng trang (`mm64.c`) để tìm PTE.
3.  **Check PTE:**
    * *Present:* Trang đang ở trong RAM $\rightarrow$ Cập nhật TLB $\rightarrow$ Truy cập.
    * *Not Present (Page Fault):* Kích hoạt xử lý lỗi trang.
4.  **Page Fault Handling:**
    * Nếu trang nằm ở Swap $\rightarrow$ **Swap In**.
    * Nếu RAM đầy $\rightarrow$ Tìm nạn nhân (FIFO) $\rightarrow$ **Swap Out** nạn nhân $\rightarrow$ Lấy Frame trống.
    * Cập nhật lại PTE và TLB.

---

## 📂 Cấu trúc mã nguồn

| File | Module | Mô tả chi tiết |
| :--- | :--- | :--- |
| **`os.c`** | Kernel Entry | Hàm `main`, khởi tạo RAM, Swap, CPU threads và nạp config. |
| **`mm64.c`** | Paging Core | Cài đặt bảng trang 5 cấp, các macro xử lý bit (`GET_VAL`, `SET_BIT`). |
| **`libmem.c`** | Mem Logic | **Core logic:** `pg_getpage` (xử lý Fault/Swap), TLB Management, `malloc`/`free`. |
| **`mm-memphy.c`** | Hardware | Giả lập phần cứng RAM/Swap device (mảng byte), hỗ trợ đọc/ghi vật lý. |
| **`sched.c`** | Scheduler | Thuật toán MLQ, quản lý Ready Queue và Run Queue. |
| **`cpu.c`** | CPU | Mô phỏng tập lệnh (Instruction Set): READ, WRITE, ALLOC, FREE. |
| **`mm-vm.c`** | VMM Helper | Quản lý các vùng nhớ ảo (VMA), `sbrk`, kiểm tra chồng lấn (overlap). |
| **`libstd.c`** | Syscall | Interface giao tiếp giữa User process và Kernel (System Calls). |

---

## 🔧 Cấu hình hệ thống

Hệ thống hoạt động dựa trên file cấu hình đầu vào. Tạo file (ví dụ `input/os_config`) với định dạng sau:

```text
[TimeSlice] [NumCPUs] [NumProcesses]
[RAM Size (Bytes)]
[Swap0 Size] [Swap1 Size] [Swap2 Size] [Swap3 Size]
[StartTime] [ProcessPath] [Priority]
[StartTime] [ProcessPath] [Priority]
...