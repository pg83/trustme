// Extracted from src/expressions/operator-expr.md:973
#![allow(unused)]
fn main() {
    struct Struct { x: u32, y: u32 }
    let (mut a, mut b) = (0, 0);
    (a, b) = (3, 4);
    
    [a, b] = [3, 4];
    
    Struct { x: a, y: b } = Struct { x: 3, y: 4};
    
    // desugars to:
    
    {
        let (_a, _b) = (3, 4);
        a = _a;
        b = _b;
    }
    
    {
        let [_a, _b] = [3, 4];
        a = _a;
        b = _b;
    }
    
    {
        let Struct { x: _a, y: _b } = Struct { x: 3, y: 4};
        a = _a;
        b = _b;
    }
}
