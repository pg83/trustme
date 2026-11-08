//@ aux-build: inline_label_dispatch.rs
extern crate inline_label_dispatch as wire;
fn main() {
    assert_eq!(wire::choose(false), 17);
    assert_eq!(wire::choose(true), 29);
}
