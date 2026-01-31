typedef struct {
    int x, y;
} Vec2;

int main() {
    Vec2 vec;
    vec.x = 1;
    vec.y = 2;
    return vec.x + vec.y != 3;
}