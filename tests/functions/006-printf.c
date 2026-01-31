int printf(const char*, ...);

int main() {
    return printf("%d %d %d %d %d %d %d %d %d %d %g %g %g %g %g %g %g %g %g %g\n",
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        1.1f, 2.2f, 3.3f, 4.4f, 5.5f, 6.6f, 7.7f, 8.8f, 9.9f, 10.0f
    ) != 60;
}