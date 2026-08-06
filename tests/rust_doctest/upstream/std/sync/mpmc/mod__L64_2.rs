// Extracted from library/std/src/sync/mpmc/mod.rs:64
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::thread;
    use std::sync::mpmc::channel;
    
    thread::scope(|s| {
        // Create a shared channel that can be sent along from many threads
        // where tx is the sending half (tx for transmission), and rx is the receiving
        // half (rx for receiving).
        let (tx, rx) = channel();
        for i in 0..10 {
            let tx = tx.clone();
            s.spawn(move || {
                tx.send(i).unwrap();
            });
        }
    
        for _ in 0..5 {
            let rx1 = rx.clone();
            let rx2 = rx.clone();
            s.spawn(move || {
                let j = rx1.recv().unwrap();
                assert!(0 <= j && j < 10);
            });
            s.spawn(move || {
                let j = rx2.recv().unwrap();
                assert!(0 <= j && j < 10);
            });
        }
    })
}
