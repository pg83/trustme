//@ aux-build: dependency_graph_exports.rs
//@ compile-variant: -O
extern crate dependency_graph_exports as exports;

#[inline(never)]
fn invoke(index: usize, value: u16) -> u16 { (exports::CALLBACKS[index])(value) }
fn main() {
    assert_eq!(invoke(0, 7), 18);
    assert_eq!(invoke(1, 13), 65);
    assert_eq!(exports::dropped(), 0);
    let guard = exports::Guard(17);
    drop(guard);
    assert_eq!(exports::dropped(), 17);
}
