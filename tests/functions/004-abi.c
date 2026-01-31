int get_value(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, float k, float l, float m, float n, float o, float p, float q, float r, float s, float t) {
    if (a != 1) return 1;
    if (b != 2) return 2;
    if (c != 3) return 3;
    if (d != 4) return 4;
    if (e != 5) return 5;
    if (f != 6) return 6;
    if (g != 7) return 7;
    if (h != 8) return 8;
    if (i != 9) return 9;
    if (j != 10) return 10;
    
    if (k != 1.1f) return 11;
    if (l != 2.2f) return 12;
    if (m != 3.3f) return 13;
    if (n != 4.4f) return 14;
    if (o != 5.5f) return 15;
    if (p != 6.6f) return 16;
    if (q != 7.7f) return 17;
    if (r != 8.8f) return 18;
    if (s != 9.9f) return 19;
    if (t != 10.0f) return 20;
    
    return 0;
}

int main() {
    return get_value(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9, 10);
}