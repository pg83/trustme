// Extracted from library/std/src/sync/mpmc/mod.rs:713
#![allow(unused)]
#![feature(mpmc_channel)]
fn main() {
    
    use std::sync::mpmc::channel;
    use std::thread;
    
    let (send, recv) = channel();
    
    thread::spawn(move || {
        send.send(1u8).unwrap();
        send.send(2u8).unwrap();
        send.send(3u8).unwrap();
    });
    
    for x in recv.iter() {
        println!("Got: {x}");
    }
}
