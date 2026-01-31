int main() {
    struct str {
        struct str* s;
        int x;
    };
    
    struct str s;
    s.s = &s;
    s.x = 0;
    
    return s.s->s->s->s->s->s->s->s->s->x;
}