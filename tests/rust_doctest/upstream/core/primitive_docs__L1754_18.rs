// Extracted from library/core/src/primitive_docs.rs:1754
#![allow(unused)]
fn main() {
    fn bar(x: i32) {}

    let not_bar_ptr = bar; // `not_bar_ptr` is zero-sized, uniquely identifying `bar`
    assert_eq!(size_of_val(&not_bar_ptr), 0);

    let bar_ptr: fn(i32) = not_bar_ptr; // force coercion to function pointer
    assert_eq!(size_of_val(&bar_ptr), size_of::<usize>());

    let footgun = &bar; // this is a shared reference to the zero-sized type identifying `bar`
}
