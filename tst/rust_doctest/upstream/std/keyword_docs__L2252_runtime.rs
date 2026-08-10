// Extracted from library/std/src/keyword_docs.rs:2252
#![allow(unused)]
fn main() {
    fn f(x: &()) -> impl Sized + use<'_> { x }
}
