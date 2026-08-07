// Extracted from library/core/src/result.rs:788
#![allow(unused)]
fn main() {
    let line = "1\n2\n3\n4\n";

    for num in line.lines() {
        match num.parse::<i32>().map(|i| i * 2) {
            Ok(n) => println!("{n}"),
            Err(..) => {}
        }
    }
}
