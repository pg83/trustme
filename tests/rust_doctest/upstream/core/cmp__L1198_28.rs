// Extracted from library/core/src/cmp.rs:1198
#![allow(unused)]
fn main() {
    #[derive(PartialEq, PartialOrd)]
    enum E {
        Top,
        Bottom,
    }
    
    assert!(E::Top < E::Bottom);
}
