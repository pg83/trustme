// Extracted from library/alloc/src/sync.rs:2732
#![allow(unused)]
#![feature(downcast_unchecked)]
extern crate alloc;
fn main() {
    
    use std::any::Any;
    use std::sync::Arc;
    
    let x: Arc<dyn Any + Send + Sync> = Arc::new(1_usize);
    
    unsafe {
        assert_eq!(*x.downcast_unchecked::<usize>(), 1);
    }
}
