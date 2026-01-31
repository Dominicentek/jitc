typedef int* intptr;

int main() {
    int val = 0;
    intptr ptr = &val;
    return *ptr;
}