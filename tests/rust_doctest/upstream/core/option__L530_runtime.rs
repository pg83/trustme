// Extracted from library/core/src/option.rs:530
#![allow(unused)]
fn main() {
    let msg = Some("howdy");
    
    // Take a reference to the contained string
    if let Some(m) = &msg {
        println!("{}", *m);
    }
    
    // Remove the contained string, destroying the Option
    let unwrapped_msg = msg.unwrap_or("default message");
}
