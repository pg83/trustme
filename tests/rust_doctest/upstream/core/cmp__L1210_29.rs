// Extracted from library/core/src/cmp.rs:1210
#![allow(unused)]
fn main() {
    #[derive(PartialEq, PartialOrd)]
    enum E {
        Top = 2,
        Bottom = 1,
    }
    
    assert!(E::Bottom < E::Top);
}
