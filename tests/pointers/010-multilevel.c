int main() {
    int x = 0;
    int* p = &x;
    int** pp = &p;
    int*** ppp = &pp;
    int**** pppp = &ppp;
    int***** ppppp = &pppp;
    return *****ppppp;
}