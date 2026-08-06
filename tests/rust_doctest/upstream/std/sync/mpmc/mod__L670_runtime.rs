// Extracted from library/std/src/sync/mpmc/mod.rs:670
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc::channel;
    use std::thread;
    use std::time::Duration;
    
    let (send, recv) = channel();
    
    let tx_thread = thread::spawn(move || {
        send.send("Hello world!").unwrap();
        thread::sleep(Duration::from_secs(2)); // block for two seconds
        send.send("Delayed for 2 seconds").unwrap();
    });
    
    let (rx1, rx2) = (recv.clone(), recv.clone());
    let rx_thread_1 = thread::spawn(move || {
        println!("{}", rx1.recv().unwrap()); // Received immediately
    });
    let rx_thread_2 = thread::spawn(move || {
        println!("{}", rx2.recv().unwrap()); // Received after 2 seconds
    });
    
    tx_thread.join().unwrap();
    rx_thread_1.join().unwrap();
    rx_thread_2.join().unwrap();
}
