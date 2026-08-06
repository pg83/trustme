// Extracted from library/alloc/src/sync.rs:188
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let arc = Arc::new(());
    // Method-call syntax
    let arc2 = arc.clone();
    // Fully qualified syntax
    let arc3 = Arc::clone(&arc);
}
