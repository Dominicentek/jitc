#include "math.h"

int main() {
    float not_a_number = 0.0f / 0.0f;
    if (isnan(not_a_number)) return 0;
    return 1;
}