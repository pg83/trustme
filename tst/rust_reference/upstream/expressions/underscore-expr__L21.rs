// Extracted from src/expressions/underscore-expr.md:21
#![allow(unused)]
fn main() {
    let p = (1, 2);
    let mut a = 0;
    (_, a) = p;
    
    struct Position {
        x: u32,
        y: u32,
    }
    
    Position { x: a, y: _ } = Position{ x: 2, y: 3 };
    
    // unused result, assignment to `_` used to declare intent and remove a warning
    _ = 2 + 2;
    // triggers unused_must_use warning
    // 2 + 2;
    
    // equivalent technique using a wildcard pattern in a let-binding
    let _ = 2 + 2;
}
