// Extracted from library/core/src/mem/mod.rs:1202
#![allow(unused)]
#![feature(never_type)]
#![feature(variant_count)]
fn main() {
    
    use std::mem;
    
    enum Void {}
    enum Foo { A(&'static str), B(i32), C(i32) }
    
    assert_eq!(mem::variant_count::<Void>(), 0);
    assert_eq!(mem::variant_count::<Foo>(), 3);
    
    assert_eq!(mem::variant_count::<Option<!>>(), 2);
    assert_eq!(mem::variant_count::<Result<!, !>>(), 2);
}
