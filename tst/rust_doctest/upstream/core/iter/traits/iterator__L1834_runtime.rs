// Extracted from library/core/src/iter/traits/iterator.rs:1834
#![allow(unused)]
fn main() {
    let lines = ["1", "2", "a"];

    let sum: i32 = lines
        .iter()
        .map(|line| line.parse::<i32>())
        .inspect(|num| {
            if let Err(ref e) = *num {
                println!("Parsing error: {e}");
            }
        })
        .filter_map(Result::ok)
        .sum();

    println!("Sum: {sum}");
}
