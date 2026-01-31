int main() {
    struct s { int x; } s;
    struct s* p = &s;
    struct s** pp = &p;
    struct s*** ppp = &pp;
    s.x = 0;
    if (s.x != 0) return 1;
    if (p.x != 0) return 1;
    if (pp.x != 0) return 1;
    if (ppp.x != 0) return 1;
    if (p->x != 0) return 1;
    if (pp->x != 0) return 1;
    if (ppp->x != 0) return 1;
    return 0;
}