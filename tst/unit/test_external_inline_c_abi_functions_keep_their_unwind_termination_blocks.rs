//@ aux-build: inline_c_boundary.rs
extern crate inline_c_boundary;
fn main() {
    assert_eq!(inline_c_boundary::choose(std::hint::black_box(1)), 97);
    assert_eq!(inline_c_boundary::checked_add(std::hint::black_box(13), 17), 30);
    let callback: extern "C" fn(usize) -> u32 = inline_c_boundary::choose;
    assert_eq!(callback(2), 101);
}
