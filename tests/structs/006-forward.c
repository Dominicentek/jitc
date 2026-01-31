int main() {
    struct str* p;
    
    struct str {
        int x;
    };
    
    struct str x;
    x.x = 0;
    p = &x;
    
    return p->x;
}