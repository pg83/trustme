// Extracted from library/std/src/sync/mpsc.rs:236
#![allow(unused)]
fn main() {
    use std::sync::mpsc::channel;
    use std::thread;
    use std::time::Duration;
    
    let (sender, receiver) = channel();
    
    // Nothing is in the buffer yet
    assert!(receiver.try_iter().next().is_none());
    println!("Nothing in the buffer...");
    
    thread::spawn(move || {
        sender.send(1).unwrap();
        sender.send(2).unwrap();
        sender.send(3).unwrap();
    });
    
    println!("Going to sleep...");
    thread::sleep(Duration::from_secs(2)); // block for two seconds
    
    for x in receiver.try_iter() {
        println!("Got: {x}");
    }
}
