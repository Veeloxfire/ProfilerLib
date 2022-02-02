#include <stdlib.h>
#include <Windows.h>
#include <stdio.h>
#include "trace.h"

void random_wait() {
  TRACING_SCOPE("random wait");

  Sleep(rand() / 100);

}

int main() {
  Tracing::start_tracer_threaded("./trace.json");

  for (int i = 0; i < 50; i++) {
    printf("%d, ", i);
    random_wait();
  }

  Tracing::end_tracer_threaded();
}