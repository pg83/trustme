// Extracted from src/items/use-declarations.md:110
#![allow(unused)]
fn main() {
    mod stuff {
        pub struct Foo(pub i32);
    }
    
    // Imports the `Foo` type and the `Foo` constructor.
    use stuff::Foo;
    
    fn example() {
        let ctor = Foo; // Uses `Foo` from the value namespace.
        let x: Foo = ctor(123); // Uses `Foo` From the type namespace.
    }
}
