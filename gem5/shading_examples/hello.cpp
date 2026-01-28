#include <iostream>
#include <string>

class circle {
public:
    char radius[8];
    char diameter[8];
};


int main(int argc, char * argv[]) {

    circle *c = new circle();
    c->radius[13] = 5;
    return 0;
}