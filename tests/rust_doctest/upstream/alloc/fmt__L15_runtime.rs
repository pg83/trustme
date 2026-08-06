// Extracted from library/alloc/src/fmt.rs:15
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        format!("Hello");                 // => "Hello"
        format!("Hello, {}!", "world");   // => "Hello, world!"
        format!("The number is {}", 1);   // => "The number is 1"
        format!("{:?}", (3, 4));          // => "(3, 4)"
        format!("{value}", value=4);      // => "4"
        let people = "Rustaceans";
        format!("Hello {people}!");       // => "Hello Rustaceans!"
        format!("{} {}", 1, 2);           // => "1 2"
        format!("{:04}", 42);             // => "0042" with leading zeros
        format!("{:#?}", (100, 200));     // => "(
                                          //       100,
                                          //       200,
                                          //     )"
        Ok(())
    }
    doctest().unwrap();
}
