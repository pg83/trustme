// Extracted from library/core/src/array/mod.rs:616
#![allow(unused)]
fn main() {
    let strings = ["Ferris".to_string(), "♥".to_string(), "Rust".to_string()];
    let is_ascii = strings.each_ref().map(|s| s.is_ascii());
    assert_eq!(is_ascii, [true, false, true]);
    
    // We can still access the original array: it has not been moved.
    assert_eq!(strings.len(), 3);
}
