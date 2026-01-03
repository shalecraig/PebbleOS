# Memory Management

PebbleOS implements a sophisticated memory management system optimized for resource-constrained embedded systems with hardware-enforced isolation.

## Heap Implementation

**Location**: `src/libutil/heap.c`

### Block Structure

Each allocation is preceded by a header:

```c
typedef struct HeapInfo {
  uint16_t PrevSize;      // Size of previous block (alignment units)
  uint16_t is_allocated:1;
  uint16_t Size:15;       // Current block size (max ~32KB)
  uint32_t pc;            // Optional: caller's PC for instrumentation
} HeapInfo_t;
```

### Allocation Strategy

**Two-way search** to reduce fragmentation:
- **Small allocations** (<256 bytes): Search from heap beginning
- **Large allocations** (≥256 bytes): Search from heap end

### Coalescing

Adjacent free blocks are automatically merged during deallocation.

## Multi-Heap Architecture

Three isolated heaps for safety:

| Heap | Location | Access | Purpose |
|------|----------|--------|---------|
| Kernel | KERNEL_RAM | Privileged only | System allocations |
| App | APP_RAM | Current app | App allocations |
| Worker | WORKER_RAM | Current worker | Worker allocations |

### Allocation Routing

```c
task_malloc(size);     // Routes to current task's heap
app_malloc(size);      // Explicit app heap
kernel_malloc(size);   // Explicit kernel heap (privileged only)
```

### Libc Integration

```c
// Standard malloc wrapped
void *__wrap_malloc(size_t size) {
  return task_malloc(size);  // Routes via task context
}
```

## Memory Layout

### Physical Memory Map

```
KERNEL_RAM
├── Kernel BSS
├── ISR Stack (with guard)
├── KernelMain Stack
├── KernelBackground Stack
└── Kernel Heap

APP_RAM
├── Stack Guard (256-1024 bytes)
├── App Stack
├── App Code (loaded from flash)
├── AppState
└── App Heap

WORKER_RAM
├── Stack Guard
├── Worker Stack
├── Worker Code
└── Worker Heap

FLASH
├── Bootloader
├── Firmware Code
├── Read-only Data
└── Resources
```

### MPU Region Configuration

| Region | Purpose | Access | Cache |
|--------|---------|--------|-------|
| Flash | Code, constants | Priv R, Unpriv R | WriteThrough |
| Readonly BSS | Kernel read-only | Priv R/W, Unpriv R | WriteBackWriteAllocate |
| ISR Stack Guard | Overflow detect | No access | NotCacheable |
| App RAM | App memory | Per-task | WriteBackWriteAllocate |
| Worker RAM | Worker memory | Per-task | WriteBackWriteAllocate |
| Task Stack Guard | Overflow detect | No access | NotCacheable |

## Per-App Memory

### Memory Segmentation

```c
// Allocate app memory regions
memory_segment_split(&segment, &stack_guard, GUARD_SIZE);
memory_segment_split(&segment, &stack, STACK_SIZE);
// Remaining segment becomes heap
heap_init(&app_heap, segment.start, segment.end);
```

### SDK-Based Limits

| SDK Version | Typical Size |
|-------------|-------------|
| 2.x (Aplite) | 60-80 KB |
| 3.x (Basalt) | 100-120 KB |
| 4.x (Chalk+) | 100-120 KB |
| Rocky.js | Reduced (~1.4KB less) |

### Stack Sizes

```c
APP_STACK_NORMAL_SIZE  = 2-4 KB
APP_STACK_ROCKY_SIZE   = 8-16 KB  // JS needs more stack
```

## Memory Protection

### Hardware Enforcement

ARM MPU isolates memory regions:

```c
// App task created with MPU regions
pebble_task_create(PebbleTask_App, &task_params, &handle);

// MPU configured per-task:
// - App can access APP_RAM
// - App cannot access KERNEL_RAM
// - App cannot access WORKER_RAM (unless worker task)
```

### Privilege Separation

```c
// Third-party apps run unprivileged
if (!is_system_app) {
  mcu_state_set_thread_privilege(false);
}

// Unprivileged code cannot:
// - Access kernel memory
// - Execute privileged instructions
// - Disable interrupts
```

### Stack Guard Protection

```c
// Stack overflow triggers MPU fault
// Guard region at bottom of stack: No access allowed
// Fault handler detects overflow, terminates app gracefully
```

## Memory Pressure Handling

### OOM Detection

```c
void *ptr = malloc(size);
if (ptr == NULL) {
  PBL_CROAK_OOM();  // Crash with diagnostic
}

// malloc_check variants return NULL instead of crashing
void *ptr = malloc_check(size);
```

### Heap Metrics

```c
size_t heap_bytes_used(Heap *heap);
size_t heap_bytes_free(Heap *heap);
size_t heap_size(Heap *heap);
size_t heap_get_minimum_headroom(Heap *heap);  // Safety margin
```

### Analytics Integration

```c
analytics_external_collect_kernel_heap_stats();  // Heartbeat
app_heap_analytics_log_native_heap_oom_fault();  // OOM events
```

## Error Detection

### Double-Free Detection

```c
// SDK-dependent behavior
if (sdk_version >= 5.1) {
  // Crash on double-free
  PBL_CROAK("Double free detected");
} else {
  // Log warning (backward compatibility)
  PBL_LOG("Warning: double free");
}
```

### Heap Corruption Detection

Block headers validated during free/coalesce:

```c
if (block->PrevSize != prev_block->Size) {
  // Corruption detected
  prv_croak_on_heap_corruption();  // Modern apps
  // or
  prv_warn_on_heap_corruption();   // Legacy apps
}
```

### Memory Fuzzing

Optional: Fill freed memory with pattern to catch use-after-free:

```c
memset(ptr, 0xBD, size);  // Fuzz on free
```

## DMA Considerations

### Cache Policy

```c
MpuCachePolicy_NotCacheable         // DMA buffers
MpuCachePolicy_WriteThrough         // Flash, LCD
MpuCachePolicy_WriteBackWriteAllocate // Normal RAM
```

### DMA Buffers

DMA transfers require non-cacheable memory to prevent stale data issues.

## Malloc Instrumentation

Optional compile-time feature for debugging:

```c
// Each allocation records caller's PC
typedef struct HeapInfo {
  ...
  uint32_t pc;  // Return address of malloc caller
} HeapInfo_t;

// Dump all allocations
heap_dump_malloc_instrumentation_to_dbgserial();

// Parse with tools/parse_dump_malloc.py
```

## Key APIs

### Kernel Heap

```c
void *kernel_malloc(size_t size);
void *kernel_zalloc(size_t size);
void *kernel_realloc(void *ptr, size_t size);
void kernel_free(void *ptr);
```

### App Heap

```c
void *app_malloc(size_t size);
void *app_zalloc(size_t size);
void *app_realloc(void *ptr, size_t size);
void app_free(void *ptr);
```

### Task-Aware

```c
void *task_malloc(size_t size);   // Current task's heap
void *task_zalloc(size_t size);
void *task_realloc(void *ptr, size_t size);
void task_free(void *ptr);

char *task_strdup(const char *s);
```

### Heap Management

```c
void heap_init(Heap *heap, void *start, void *end);
void heap_lock(Heap *heap);
void heap_unlock(Heap *heap);
```

## Design Principles

1. **Isolation**: App crashes don't corrupt kernel
2. **Predictability**: Fixed heap per SDK version
3. **Security**: MPU prevents unauthorized access
4. **Debugging**: Instrumentation, corruption detection
5. **Efficiency**: Two-way search reduces fragmentation

## Typical Constraints

| Resource | Typical Value |
|----------|---------------|
| Total RAM | 64-256 KB |
| App heap | 10-20 KB free |
| Kernel heap | 5-10 KB reserved |
| Framebuffer | 2-6 KB |
| Stack | 2-8 KB per task |
