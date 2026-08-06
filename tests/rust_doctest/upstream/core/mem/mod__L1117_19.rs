// Extracted from library/core/src/mem/mod.rs:1117
#![allow(unused)]
fn main() {
    use std::mem;
    
    enum Foo { A(&'static str), B(i32), C(i32) }
    
    assert_eq!(mem::discriminant(&Foo::A("bar")), mem::discriminant(&Foo::A("baz")));
    assert_eq!(mem::discriminant(&Foo::B(1)), mem::discriminant(&Foo::B(2)));
    assert_ne!(mem::discriminant(&Foo::B(3)), mem::discriminant(&Foo::C(3)));
}
