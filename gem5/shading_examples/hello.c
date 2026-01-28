#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct line_t
{
  int length;
} line;

typedef struct rectangle_t
{
  int width;
  int height;
} rectangle;


// void print_walls(rectangle w) {
//   printf("Width: %d\n", w.width);
//   printf("Height: %d\n", w.height);
// }
typedef struct
{
  int x, y, info, dx, dy, I;
} CORNER_LIST[8];
// void corner_draw(in, corner_list, x_size, drawing_mode) char *in;
// CORNER_LIST corner_list;
// int x_size, drawing_mode;
// {
//   char *p;
//   int n = 0;

//   p = in + (corner_list[n].y - 1) * x_size + corner_list[n].x - 1;
// } /* struct used in some test cases as a custom type */
typedef struct _twoIntsStruct
{
  int intOne;
  int intTwo;
} twoIntsStruct;

void printStructLine (const twoIntsStruct * structTwoIntsStruct)
{
    printf("%d -- %d\n", structTwoIntsStruct->intOne, structTwoIntsStruct->intTwo);
}

void printIntLine(int intOne)
{
    printf("%d\n", intOne);
}

typedef struct house_t
{
  int length;
  int num[4];
  rectangle walls;
} house;

typedef struct street_t
{
  int test;
  house house_member;
} street;

int main(void)
{
  house *h = malloc(sizeof(house));
  printf("%p\n", h->num);
  h->num[4] = 1;
  street *s = malloc(sizeof(street));
  // printf("%p\n", s);
  // printf("%p\n", &(s->house_member));
  // printf("%p\n", s->house_member.num);
  // s->house_member.num[4] = 1;
}