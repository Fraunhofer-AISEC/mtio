#include <stdio.h>
#include <stdlib.h>


struct user {
	char username[16];
	char password[16];
};

int main(void) {
	struct user *user_p = malloc(32);
	for (int i = 0; i < 20; i++) {
		user_p->username[i] = 42;
	}
}
