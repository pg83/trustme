// Extracted from library/alloc/src/vec/drain.rs:74
#![allow(unused)]
#![feature(drain_keep_rest)]
extern crate alloc;
fn main() {

    let mut vec = vec!['a', 'b', 'c'];
    let mut drain = vec.drain(..);

    assert_eq!(drain.next().unwrap(), 'a');

    // This call keeps 'b' and 'c' in the vec.
    drain.keep_rest();

    // If we wouldn't call `keep_rest()`,
    // `vec` would be empty.
    assert_eq!(vec, ['b', 'c']);
}
