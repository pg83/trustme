// Extracted from library/core/src/macros/mod.rs:1196
#![allow(unused)]
fn main() {
    let a = ("foobar", column!()).1;
    let b = ("人之初性本善", column!()).1;
    let c = ("f̅o̅o̅b̅a̅r̅", column!()).1; // Uses combining overline (U+0305)
    
    assert_eq!(a, b);
    assert_ne!(b, c);
}
