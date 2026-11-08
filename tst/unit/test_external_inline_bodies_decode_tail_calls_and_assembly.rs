//@ aux-build: inline_tail_and_asm.rs
extern crate inline_tail_and_asm as wire;
fn main() {
    assert_eq!(wire::tail(6), 7);
    assert_eq!(wire::unchanged(11), 11);
}
