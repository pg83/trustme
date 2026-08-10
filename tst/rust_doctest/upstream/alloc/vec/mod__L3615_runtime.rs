// Extracted from library/alloc/src/vec/mod.rs:3615
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Mutex;
    static LONG_LIVED: Mutex<Vec<Vec<u16>>> = Mutex::new(Vec::new());

    for i in 0..10 {
        let big_temporary: Vec<u16> = (0..1024).collect();
        // discard most items
        let mut result: Vec<_> = big_temporary.into_iter().filter(|i| i % 100 == 0).collect();
        // without this a lot of unused capacity might be moved into the global
        result.shrink_to_fit();
        LONG_LIVED.lock().unwrap().push(result);
    }
}
