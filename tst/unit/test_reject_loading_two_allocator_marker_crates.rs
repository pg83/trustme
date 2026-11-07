//@ compile-fail: Multiple allocator crates loaded
//@ aux-build: allocator_marker_left.rs
//@ aux-build: allocator_marker_right.rs

extern crate allocator_marker_left;
extern crate allocator_marker_right;

fn main() {
    let _ = (allocator_marker_left::marker(), allocator_marker_right::marker());
}
