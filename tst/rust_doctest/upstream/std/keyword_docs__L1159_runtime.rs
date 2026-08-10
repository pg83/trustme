// Extracted from library/std/src/keyword_docs.rs:1159
#![allow(unused)]
fn main() {
    let maybe_name = Some(String::from("Alice"));
    // Using `ref`, the value is borrowed, not moved ...
    match maybe_name {
        Some(ref n) => println!("Hello, {n}"),
        _ => println!("Hello, world"),
    }
    // ... so it's available here!
    println!("Hello again, {}", maybe_name.unwrap_or("world".into()));
}
