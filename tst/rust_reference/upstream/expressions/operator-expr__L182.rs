// Extracted from src/expressions/operator-expr.md:182
#![allow(unused)]
fn main() {
    struct NoCopy;
    let a = &7;
    assert_eq!(*a, 7);
    let b = &mut 9;
    *b = 11;
    assert_eq!(*b, 11);
    let c = Box::new(NoCopy);
    let d: NoCopy = *c;
}
