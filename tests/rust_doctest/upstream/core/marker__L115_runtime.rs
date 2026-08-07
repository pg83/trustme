// Extracted from library/core/src/marker.rs:115
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    struct Foo<T>(T);
    struct Bar<T: ?Sized>(T);

    // struct FooUse(Foo<[i32]>); // error: Sized is not implemented for [i32]
    struct BarUse(Bar<[i32]>); // OK
}
