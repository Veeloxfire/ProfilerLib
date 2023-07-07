#include "trace.h"

#include <Windows.h>
#include <assert.h>
#include <intrin.h>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

volatile static u32 signal = 0;
volatile static u64 overhead = 0;

struct InternalEvent {
  DWORD thread_id;

  u32 name_size;
  const char* name;

  u64 time_start;
  u64 time_end;
};

static constexpr u64 BUFFER_NUM = 2048;
static InternalEvent* volatile buffer;
static InternalEvent* volatile other_buffer;
static volatile u64 buffer_top = 0;
static volatile char buffer_mutex = '\0';

static u32 want_buffer_signal = 0;
static void get_buffer_priority(bool is_tracer) {
  if (is_tracer) {
    auto val = _InterlockedExchange(&want_buffer_signal, 1);
    assert(val != 1);//only 1 tracer thread for now

    while (true) {
      char r = _InterlockedCompareExchange8(&buffer_mutex, '\1', '\0');
      if (r == 1) {
        val = _InterlockedExchange(&want_buffer_signal, 0);
        assert(val != 0);//only 1 tracer thread for now
        return;
      }
    }
  }
  else {
    while (true) {
      if (want_buffer_signal) continue;

      char r = _InterlockedCompareExchange8(&buffer_mutex, '\1', '\0');
      if (r == 1) {
        return;
      }
    }
  }
}

static void release_buffer() {
  _InterlockedCompareExchange8(&buffer_mutex, '\0', '\1');
}


static bool comma = false;
static HANDLE out_file_handle = INVALID_HANDLE_VALUE;
static Tracing::u64 start_timer = 0;
static Tracing::u64 freq = 0;

static constexpr u32 OUT_BUFFER_SIZE = 1024 * 16;
static u32 out_buffer_top = 0;
static volatile u32 thread_counter = 0;
static u8* out_buffer;

static u64 num_profiles = 0;

static HANDLE thread_handle = INVALID_HANDLE_VALUE;

static void flush_buffer() {
  WriteFile(out_file_handle, out_buffer, out_buffer_top, NULL, 0);
  out_buffer_top = 0;
}

static void write_u16(u16 u) {
  if (out_buffer_top + 2 > OUT_BUFFER_SIZE) {
    flush_buffer();
  }

  u8* buffer = out_buffer + out_buffer_top;

  buffer[0] = u & 0xff;
  buffer[1] = (u >> 8) & 0xff;

  out_buffer_top += 2;
}

static void write_u32(u32 u) {
  if (out_buffer_top + 4 > OUT_BUFFER_SIZE) {
    flush_buffer();
  }

  u8* buffer = out_buffer + out_buffer_top;

  buffer[0] = u & 0xff;
  buffer[1] = (u >> 8) & 0xff;
  buffer[2] = (u >> 16) & 0xff;
  buffer[3] = (u >> 24) & 0xff;

  out_buffer_top += 4;
}

static void write_u64(u64 u) {
  if (out_buffer_top + 8 > OUT_BUFFER_SIZE) {
    flush_buffer();
  }

  u8* buffer = out_buffer + out_buffer_top;

  buffer[0] = u & 0xff;
  buffer[1] = (u >> 8) & 0xff;
  buffer[2] = (u >> 16) & 0xff;
  buffer[3] = (u >> 24) & 0xff;
  buffer[4] = (u >> 32) & 0xff;
  buffer[5] = (u >> 40) & 0xff;
  buffer[6] = (u >> 48) & 0xff;
  buffer[7] = (u >> 56) & 0xff;

  out_buffer_top += 8;
}

static void write_string(u32 name_size, const char* name) {
  bool extra_null = name[name_size - 1] != '\0';

  if (name_size + extra_null + out_buffer_top > OUT_BUFFER_SIZE) {
    flush_buffer();
  }

  {
    for (u32 i = 0; i < name_size; ++i) {
      out_buffer[out_buffer_top + i] = name[i];
    }

    if (extra_null) {
      out_buffer[out_buffer_top + name_size] = '\0';
      out_buffer_top += name_size + 1;
    }
  }
}

static void write_single(const InternalEvent& e) {
  num_profiles += 1;

  write_u16(static_cast<u16>(e.thread_id));
  write_u32(e.name_size);
  write_string(e.name_size, e.name);
  write_u64(e.time_start);
  write_u64(e.time_end);
}

static constexpr u8 VERSION = 0;

struct Header {
  u8 version = VERSION;
};

static void write_header(const Header& header) {
  out_buffer[out_buffer_top] = header.version;
  out_buffer_top += 1;
}

struct Footer {
  u16 num_threads;
  u64 num_profiles;
};

static void write_footer(const Footer& footer) {
  write_u16(footer.num_threads);
  write_u64(footer.num_profiles);
}

static DWORD WINAPI tracer_thread_proc(LPVOID lpParameter) {
  Header header = {};
  write_header(header);

  while (true) {
    if (signal == 1) {
      const auto ed = Tracing::get_time();

      get_buffer_priority(true);
      u32 top = (u32)buffer_top;
      for (u32 i = 0; i < top; i++) {
        write_single(buffer[i]);
      }
      release_buffer();

      Footer footer = {};
      footer.num_threads = thread_counter;
      footer.num_profiles = num_profiles;

      write_footer(footer);
      flush_buffer();
      return 0;
    }

    if (buffer_top > 20) {
      get_buffer_priority(true);
      MemoryBarrier();
      const auto size = _InterlockedExchange(&buffer_top, 0);
      auto save = buffer;
      buffer = other_buffer;
      other_buffer = save;
      release_buffer();

      for (u32 i = 0; i < size; i++) {
        write_single(other_buffer[i]);
      }
    }
  }
}

void Tracing::start_tracer_threaded(const char* output_file_name) {
  overhead = 0;

  LARGE_INTEGER la = {};

  QueryPerformanceFrequency(&la);

  freq = la.QuadPart / (1000 * 1000);

  QueryPerformanceCounter(&la);

  start_timer = la.QuadPart / freq;

  out_file_handle = CreateFileA(output_file_name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  assert(out_file_handle != INVALID_HANDLE_VALUE);

  buffer = (InternalEvent*)VirtualAlloc(0, sizeof(InternalEvent) * BUFFER_NUM, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  assert(buffer);
  memset(buffer, 0, BUFFER_NUM * sizeof(Event));

  other_buffer = (InternalEvent*)VirtualAlloc(0, sizeof(InternalEvent) * BUFFER_NUM, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  assert(other_buffer);
  memset(other_buffer, 0, BUFFER_NUM * sizeof(Event));

  out_buffer = (u8*)VirtualAlloc(0, OUT_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  assert(out_buffer);
  memset(out_buffer, 0, OUT_BUFFER_SIZE);

  const auto check = _InterlockedCompareExchange(&buffer_top, 0, 0);
  assert(check == 0);

  thread_handle = CreateThread(0, 0, tracer_thread_proc, 0, 0, 0);
}

void Tracing::end_tracer_threaded() {
  //Signal the end of the other thread
  auto val = _InterlockedExchange(&signal, 1);
  assert(val == 0);

  WaitForSingleObject(thread_handle, INFINITE);

  VirtualFree(buffer, 0, MEM_RELEASE);
  VirtualFree(other_buffer, 0, MEM_RELEASE);
  CloseHandle(out_file_handle);
}

Tracing::u64 Tracing::get_time() {
  LARGE_INTEGER la = {};

  QueryPerformanceCounter(&la);

  return (la.QuadPart / freq) - start_timer;
}

thread_local static u16 thread_id;

void Tracing::new_traced_thread() {
  thread_id = static_cast<u16>(_InterlockedIncrement(&thread_counter)) - 1;
}

void Tracing::upload_event(const Event& e) {
  InternalEvent ie = {};
  ie.thread_id = thread_id;
  ie.name = e.name;
  ie.name_size = e.name_size;
  ie.time_start = e.time_start;
  ie.time_end = e.time_end;

  get_buffer_priority(false);
  MemoryBarrier();
  u32 new_top = (u32)_InterlockedIncrement(&buffer_top);
  assert(!(new_top > BUFFER_NUM));
  buffer[new_top - 1] = ie;
  release_buffer();
}
