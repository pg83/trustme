// Extracted from library/core/src/bool.rs:48
#![allow(unused)]
fn main() {
    let mut a = 0;
    
    true.then(|| { a += 1; });
    false.then(|| { a += 1; });
    
    // `a` is incremented once because the closure is evaluated lazily by
    // `then`.
    assert_eq!(a, 1);
}
