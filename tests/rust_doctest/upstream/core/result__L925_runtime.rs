// Extracted from library/core/src/result.rs:925
#![allow(unused)]
fn main() {
    let x: u8 = "4"
        .parse::<u8>()
        .inspect(|x| println!("original: {x}"))
        .map(|x| x.pow(3))
        .expect("failed to parse number");
}
