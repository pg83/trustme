// Extracted from library/std/src/sync/poison/once.rs:269
#![allow(unused)]
fn main() {
    use std::sync::Once;
    use std::thread;
    
    static READY: Once = Once::new();
    
    let thread = thread::spawn(|| {
        READY.wait();
        println!("everything is ready");
    });
    
    READY.call_once(|| println!("performing setup"));
}
