// Extracted from library/core/src/cell/once.rs:306
#![allow(unused)]
fn main() {
    use std::cell::OnceCell;
    
    let cell: OnceCell<String> = OnceCell::new();
    assert_eq!(cell.into_inner(), None);
    
    let cell = OnceCell::new();
    let _ = cell.set("hello".to_owned());
    assert_eq!(cell.into_inner(), Some("hello".to_owned()));
}
