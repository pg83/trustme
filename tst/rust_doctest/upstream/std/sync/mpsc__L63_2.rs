// Extracted from library/std/src/sync/mpsc.rs:63
#![allow(unused)]
fn main() {
    use std::thread;
    use std::sync::mpsc::channel;

    // Create a shared channel that can be sent along from many threads
    // where tx is the sending half (tx for transmission), and rx is the receiving
    // half (rx for receiving).
    let (tx, rx) = channel();
    for i in 0..10 {
        let tx = tx.clone();
        thread::spawn(move || {
            tx.send(i).unwrap();
        });
    }

    for _ in 0..10 {
        let j = rx.recv().unwrap();
        assert!(0 <= j && j < 10);
    }
}
