#include <stdio.h>
#include <stdlib.h>


int main(void) {
	char *buf1 = malloc(16);
	for (int i = 0; i < 20; i++) {
		buf1[i] = 42;
	}
}
