// Extracted from library/std/src/thread/mod.rs:304
#![allow(unused)]
fn main() {
    use std::thread;
    
    let builder = thread::Builder::new()
        .name("foo".into());
    
    let handler = builder.spawn(|| {
        assert_eq!(thread::current().name(), Some("foo"))
    }).unwrap();
    
    handler.join().unwrap();
}
