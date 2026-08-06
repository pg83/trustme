// Extracted from src/arc-mutex/arc-layout.md:26
#![allow(unused)]
fn main() {
    use std::sync::atomic;
    
    pub struct Arc<T> {
        ptr: *mut ArcInner<T>,
    }
    
    pub struct ArcInner<T> {
        rc: atomic::AtomicUsize,
        data: T,
    }
}
