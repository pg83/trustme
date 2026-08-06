// Extracted from library/std/src/sync/poison/mutex.rs:598
#![allow(unused)]
fn main() {
    use std::sync::Mutex;
    
    let mut mutex = Mutex::new(0);
    *mutex.get_mut().unwrap() = 10;
    assert_eq!(*mutex.lock().unwrap(), 10);
}
