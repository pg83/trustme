// Extracted from library/core/src/marker.rs:132
#![allow(unused)]
#![allow(unused_variables)]
fn main() {
    trait Foo { }
    trait Bar: Sized { }
    
    struct Impl;
    impl Foo for Impl { }
    impl Bar for Impl { }
    
    let x: &dyn Foo = &Impl;    // OK
    // let y: &dyn Bar = &Impl; // error: the trait `Bar` cannot
                                // be made into an object
}
