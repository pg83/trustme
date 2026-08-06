// Extracted from library/core/src/iter/mod.rs:328
#![allow(unused)]
fn main() {
    let numbers = 0..;
    let five_numbers = numbers.take(5);
    
    for number in five_numbers {
        println!("{number}");
    }
}
