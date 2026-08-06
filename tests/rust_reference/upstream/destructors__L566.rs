// Extracted from src/destructors.md:566
#![allow(unused)]
fn main() {
    fn temp() {}
    trait Use { fn use_temp(&self) -> &Self { self } }
    impl Use for () {}
    // Receivers of method calls are not extending expressions.
    let x = (&temp()).use_temp(); // ERROR
    x;
}
