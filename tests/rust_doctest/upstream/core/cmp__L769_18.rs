// Extracted from library/core/src/cmp.rs:769
#![allow(unused)]
fn main() {
    #[derive(PartialEq, Eq, PartialOrd, Ord)]
    enum E {
        Top = 2,
        Bottom = 1,
    }
    
    assert!(E::Bottom < E::Top);
}
