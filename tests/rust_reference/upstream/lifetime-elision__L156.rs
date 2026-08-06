// Extracted from src/lifetime-elision.md:156
#![allow(unused)]
fn main() {
    // For the following trait...
    trait Bar<'a>: 'a { }
    
    // ...these two are the same:
    type T1<'a> = Box<dyn Bar<'a>>;
    type T2<'a> = Box<dyn Bar<'a> + 'a>;
    
    // ...and so are these:
    impl<'a> dyn Bar<'a> {}
    impl<'a> dyn Bar<'a> + 'a {}
}
