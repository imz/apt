#include <config.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
   if (sizeof(off_t) != sizeof(long))
      printf("-%lu",8 * (unsigned long)sizeof(off_t));
   exit(0);
}
