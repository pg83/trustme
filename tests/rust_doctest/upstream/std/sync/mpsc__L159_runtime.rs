// Extracted from library/std/src/sync/mpsc.rs:159
#![allow(unused)]
fn main() {
    use std::sync::mpsc::channel;
    use std::thread;
    use std::time::Duration;
    
    let (send, recv) = channel();
    
    thread::spawn(move || {
        send.send("Hello world!").unwrap();
        thread::sleep(Duration::from_secs(2)); // block for two seconds
        send.send("Delayed for 2 seconds").unwrap();
    });
    
    println!("{}", recv.recv().unwrap()); // Received immediately
    println!("Waiting...");
    println!("{}", recv.recv().unwrap()); // Received after 2 seconds
}
