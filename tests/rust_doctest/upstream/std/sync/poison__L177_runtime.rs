// Extracted from library/std/src/sync/poison.rs:177
#![allow(unused)]
fn main() {
    use std::sync::{Arc, Mutex};
    use std::thread;

    let mutex = Arc::new(Mutex::new(1));

    // poison the mutex
    let c_mutex = Arc::clone(&mutex);
    let _ = thread::spawn(move || {
        let mut data = c_mutex.lock().unwrap();
        *data = 2;
        panic!();
    }).join();

    match mutex.lock() {
        Ok(_) => unreachable!(),
        Err(p_err) => {
            let data = p_err.get_ref();
            println!("recovered: {data}");
        }
    };
}
