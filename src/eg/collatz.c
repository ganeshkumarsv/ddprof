#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>


int collatz(int n) {
  while (1) {};
  return 0;
}

int main(int argc, char **argv) {
  int max = 10;
  if (1 < argc)
    max = atoi(argv[1]);
  int n = 1;
  while (1) {
    //    if(fork())
    //      return 0;
    for (int i = 1; i < max; i++)
      collatz(i);
    for (int j = 0; j < 10 * n; j++) {
      __asm__ volatile("pause");
      usleep(1);
    }
  }
}