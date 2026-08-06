// Extracted from src/items/use-declarations.md:288
#![allow(unused)]
fn main() {
    // This creates a binding to the `clashing::Foo` tuple struct
    // constructor, but does not import its type because that would
    // conflict with the `Foo` struct defined here.
    //
    // Note that the order of definition here is unimportant.
    use clashing::*;
    struct Foo {
        field: f32,
    }
    
    fn do_stuff() {
        // Uses the constructor from `clashing::Foo`.
        let f1 = Foo(123);
        // The struct expression uses the type from
        // the `Foo` struct defined above.
        let f2 = Foo { field: 1.0 };
        // `Bar` is also in scope due to the glob import.
        let z = Bar {};
    }
    
    mod clashing {
        pub struct Foo(pub i32);
        pub struct Bar {}
    }
}
