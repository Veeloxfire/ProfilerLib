#include <cstdlib>
#include <cstdio>

#include <Windows.h>

#include <Tracer/trace.h>

void random_wait() {
  TRACING_SCOPE("random wait");

  Sleep(rand() / 100);

}

int main() {
  Tracing::start_tracer_threaded("./trace.trace");

  for (int i = 0; i < 50; i++) {
    printf("%d, ", i);
    random_wait();
  }

  Tracing::end_tracer_threaded();
}