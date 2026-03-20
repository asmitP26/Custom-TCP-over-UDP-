# 🚀 KTP Socket – Reliable Data Transfer over UDP

## 📌 About the Project

This project implements a custom transport protocol called **KTP (KGP Transport Protocol)** on top of **UDP sockets**.

We know that UDP is **unreliable**:

* No guarantee of delivery ❌
* No ordering ❌
* No retransmission ❌

So, in this project, we build our own mechanism to provide:

* ✅ Reliable delivery
* ✅ In-order message transfer
* ✅ Flow control using sliding window
* ✅ Retransmission on timeout

Basically, we are recreating **some core ideas of TCP**, but in a simpler and controlled way.

---

## 🧠 Key Idea

Instead of sending raw bytes like TCP, **KTP is message-oriented**.

* Each message = **512 bytes**
* Each message has a **sequence number**
* Sender and receiver maintain:

  * **Send Window (swnd)**
  * **Receive Window (rwnd)**

---

## 🏗️ Project Structure

```text
.
├── ksocket.h        # All structures, constants, and function declarations
├── ksocket.c        # Implementation of KTP APIs
├── initksocket.c    # Initializes shared memory, threads (R & S), garbage collector
├── user1.c          # Sender program (reads file and sends)
├── user2.c          # Receiver program (receives and writes file)
├── Makefile_lib     # Builds static library
├── Makefile_init    # Builds init process
├── Makefile_user    # Builds user programs
```

---

## ⚙️ How It Works

### 🔹 Sender Side

1. Application calls `k_sendto()`
2. Message stored in **send buffer**
3. Assigned a **sequence number**
4. Thread **S** sends messages via UDP
5. If ACK not received → **retransmission happens**

---

### 🔹 Receiver Side

1. Thread **R** receives UDP packets
2. Stores them in **receive buffer**
3. Handles:

   * Out-of-order packets
   * Duplicate packets
4. Sends **ACK with rwnd (flow control)**
5. Application calls `k_recvfrom()` → gets data **in order**

---

### 🔹 Threads

#### 🧵 Thread R (Receiver Thread)

* Listens on UDP sockets using `select()`
* Handles:

  * DATA messages
  * ACK messages
  * Duplicate ACK
  * Buffer full condition (`nospace`)

---

#### 🧵 Thread S (Sender Thread)

* Runs periodically
* Handles:

  * Sending new messages
  * Retransmitting timed-out messages

---

#### ♻️ Garbage Collector

* Cleans socket entries if process dies unexpectedly

---

## 🔁 Sliding Window Concept

* Sender can send multiple messages without waiting for ACK
* Receiver controls flow using `rwnd`
* ACK is **cumulative**

Example:

```text
Received: 1,2,3
ACK sent: 3
```

---

## 📉 Simulating Packet Loss

We simulate an unreliable network using:

```c
int dropMessage(float p);
```

* `p` = probability of dropping a packet
* Helps test reliability and retransmission

---

## 🧪 How to Run

### Step 1 – Build everything

```bash
make -f Makefile_lib
make -f Makefile_init
make -f Makefile_user
```

---

### Step 2 – Run programs (in 3 terminals)

```bash
# Terminal 1
./initksocket

# Terminal 2
./user2

# Terminal 3
./user1
```

---

### Step 3 – Verify output

```bash
diff input.txt output.txt
```

If nothing prints → ✅ successful transfer

---

## 📊 Experimentation

You can vary packet loss probability in `ksocket.h`:

```c
#define P 0.05
```

Test values:

```text
0.05, 0.1, 0.15, ..., 0.5
```

Measure:

```text
Average transmissions per message
```

---

## 💡 What I Learned

* How **reliable protocols like TCP actually work internally**
* Sliding window and flow control concepts
* Handling:

  * Packet loss
  * Out-of-order delivery
  * Duplicate packets
* Shared memory and inter-process communication
* Multi-threading with synchronization

---

## ⚠️ Challenges Faced

* Managing shared memory correctly across processes
* Handling sequence numbers properly
* Debugging infinite loops and missing ACKs
* Ensuring in-order delivery despite unordered arrival

---

## 🎯 Conclusion

This project gave hands-on experience in building a **reliable transport protocol from scratch**.

It bridges the gap between theory (like TCP concepts) and real implementation.

---

## 👨‍💻 Author

**Asmit Pandey**

---

## ⭐ If you like this project

Feel free to ⭐ the repo and share feedback!
