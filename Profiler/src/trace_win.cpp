#include "trace.h"

#include <Windows.h>
#include <assert.h>
#include <intrin.h>

using u32 = uint32_t;
using u64 = uint64_t;

static u32 signal = 0;
static u64 overhead = 0;

static constexpr u64 BUFFER_NUM = 2048;
static Tracing::Event* buffer;
static u64 buffer_top = 0;

static bool comma = false;
static HANDLE out_file_handle = INVALID_HANDLE_VALUE;
static Tracing::u64 start_timer = 0;
static Tracing::u64 freq = 0;

#define WRITE_STATIC_STRING(file, str) WriteFile(file, str, sizeof(str) - 1, 0, 0)

#define INTERLOCKED_READ(var) _InterlockedCompareExchange(&var, 0, 0)

static constexpr Tracing::u64 DEC_DIGITS[]{
  1,
  10,
  100,
  1000,
  10000,
  100000,
  1000000,
  10000000,
  100000000,
  1000000000,
  10000000000,
  100000000000,
  1000000000000,
  10000000000000,
  100000000000000,
  1000000000000000,
  10000000000000000,
  100000000000000000,
  1000000000000000000,
  10000000000000000000,
};

template<typename T, size_t N>
inline constexpr static size_t array_count(const T (&)[N]) {
  return N;
}

static void write_decimal_number(HANDLE file, Tracing::u64 num) {
  if (num == 0) {
    WriteFile(file, "0", 1, 0, 0);
    return;
  }

  //array count + 1 for null
  char buffer[array_count(DEC_DIGITS) + 1] ={ 0 };
  u64 index = 0;

  //Array count + 1 so that 0 can signal the finish
  u64 max_ind = array_count(DEC_DIGITS) + 1;

  //SHouldnt need to check if max_ind > 0 because num will always have digits
  while (DEC_DIGITS[max_ind - 1] > num) {
    max_ind -= 1;
  }

  while (max_ind > 0) {
    const auto d_val = DEC_DIGITS[max_ind - 1];
    Tracing::u64 dig = num / d_val;
    num -= (dig * d_val);

    buffer[index] = '0' + (char)dig;

    max_ind -= 1;
    index += 1;
  }

  //Index is now the size
  WriteFile(file, buffer, index, 0, 0);
  return;
}

void write_single(const Tracing::Event& e) {
  WRITE_STATIC_STRING(out_file_handle, ",\n    {\"name\":\"");
  WriteFile(out_file_handle, e.name, strlen(e.name), 0, 0);
  WRITE_STATIC_STRING(out_file_handle, "\",\"pid\":0,\"ph\":\"B\",\"ts\":");

  write_decimal_number(out_file_handle, e.time_start);

  WRITE_STATIC_STRING(out_file_handle, "},\n    {\"name\":\"");
  WriteFile(out_file_handle, e.name, strlen(e.name), 0, 0);
  WRITE_STATIC_STRING(out_file_handle, "\",\"pid\":0,\"ph\":\"E\",\"ts\":");

  write_decimal_number(out_file_handle, e.time_end);
  WRITE_STATIC_STRING(out_file_handle, "}");
}

static DWORD WINAPI tracer_thread_proc(LPVOID lpParameter) {
  WRITE_STATIC_STRING(out_file_handle, "{\n  \"traceEvents\": [\n    {\"name\":\"Tracing Total\",\"pid\":0,\"ph\":\"B\",\"ts\":0}");

  while (true) {
    const auto count = INTERLOCKED_READ(buffer_top);

    if (INTERLOCKED_READ(signal) == 1) {
      const auto ed = Tracing::get_time();

      MemoryBarrier();
      for (u32 i = 0; i < count; i++) {
        write_single(buffer[i]);
      }

      WRITE_STATIC_STRING(out_file_handle, ",\n    {\"name\":\"Tracing Total\",\"pid\":0,\"ph\":\"E\",\"ts\":");
      write_decimal_number(out_file_handle, ed);

      WRITE_STATIC_STRING(out_file_handle, "}\n  ]\n\n}");

      CloseHandle(out_file_handle);
      VirtualFree(buffer, 0, MEM_RELEASE);
      return 0;
    }


    if (count == 0) {
      Sleep(0);
      continue;
    }

    MemoryBarrier();
    Tracing::Event e = buffer[count - 1];

    if (_InterlockedCompareExchange(&buffer_top, count - 1, count) != count) {
      //Need to retry as someone else made an edit
      continue;
    }

    //Success
    write_single(e);
  }
}

void Tracing::start_tracer_threaded(const char* output_file_name) {
  overhead = 0;

  LARGE_INTEGER la ={};

  QueryPerformanceFrequency(&la);

  freq = la.QuadPart / 1000;

  QueryPerformanceCounter(&la);

  start_timer = la.QuadPart / freq;

  out_file_handle = CreateFileA(output_file_name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  assert(out_file_handle != INVALID_HANDLE_VALUE);

  buffer = (Event*)VirtualAlloc(0, sizeof(Event) * BUFFER_NUM, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  assert(buffer);
  memset(buffer, 0, BUFFER_NUM *  sizeof(Event));

  const auto check = _InterlockedCompareExchange(&buffer_top, 0, 0);
  assert(check == 0);

  CreateThread(0, 0, tracer_thread_proc, 0, 0, 0);
}

void Tracing::end_tracer_threaded() {
  //Signal the end of the other thread
  auto val = _InterlockedExchange(&signal, 1);
  assert(val == 0);

  //return INTERLOCKED_READ(overhead);
}

Tracing::u64 Tracing::get_time() {
  LARGE_INTEGER la ={};

  QueryPerformanceCounter(&la);

  return (la.QuadPart / freq) - start_timer;
}

void Tracing::upload_event(const Event& e) {
  const auto val = _InterlockedIncrement(&buffer_top);
  assert(val <= BUFFER_NUM);

  buffer[val - 1] = e;
  MemoryBarrier();


  //_interlockedadd64((volatile long long*)&overhead, st - ed);
}
