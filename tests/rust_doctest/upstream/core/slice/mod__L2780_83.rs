// Extracted from library/core/src/slice/mod.rs:2780
#![allow(unused)]
#![feature(trim_prefix_suffix)]
fn main() {
    
    let v = &[10, 40, 30];
    
    // Suffix present - removes it
    assert_eq!(v.trim_suffix(&[30]), &[10, 40][..]);
    assert_eq!(v.trim_suffix(&[40, 30]), &[10][..]);
    assert_eq!(v.trim_suffix(&[10, 40, 30]), &[][..]);
    
    // Suffix absent - returns original slice
    assert_eq!(v.trim_suffix(&[50]), &[10, 40, 30][..]);
    assert_eq!(v.trim_suffix(&[50, 30]), &[10, 40, 30][..]);
}
