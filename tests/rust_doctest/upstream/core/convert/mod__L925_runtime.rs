// Extracted from library/core/src/convert/mod.rs:925
#![allow(unused)]
fn main() {
    trait MyTrait {}
    impl MyTrait for fn() -> ! {}
    impl MyTrait for fn() -> std::convert::Infallible {}
}
