// Extracted from library/core/src/clone.rs:186
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    
    let data = Arc::new(Mutex::new(vec![1, 2, 3]));
    let data_clone = data.clone(); // Creates another Arc pointing to the same Mutex
    
    {
        let mut lock = data.lock().unwrap();
        lock.push(4);
    }
    
    // Changes are visible through the clone because they share the same underlying data
    assert_eq!(*data_clone.lock().unwrap(), vec![1, 2, 3, 4]);
}
