// Extracted from library/core/src/cmp.rs:757
#![allow(unused)]
fn main() {
    #[derive(PartialEq, Eq, PartialOrd, Ord)]
    enum E {
        Top,
        Bottom,
    }

    assert!(E::Top < E::Bottom);
}
