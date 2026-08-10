// Extracted from src/lifetime-elision.md:140
#![allow(unused)]
fn main() {
    // This is an example of an error.
    trait Foo { }
    struct TwoBounds<'a, 'b, T: ?Sized + 'a + 'b> {
        f1: &'a i32,
        f2: &'b i32,
        f3: T,
    }
    type T7<'a, 'b> = TwoBounds<'a, 'b, dyn Foo>;
    //                                  ^^^^^^^
    // Error: the lifetime bound for this object type cannot be deduced from context
}
