// Extracted from library/std/src/sync/nonpoison/mutex.rs:356
#![allow(unused)]
#![feature(nonpoison_mutex)]
fn main() {
    
    use std::sync::nonpoison::Mutex;
    
    let mut mutex = Mutex::new(0);
    *mutex.get_mut() = 10;
    assert_eq!(*mutex.lock(), 10);
}
