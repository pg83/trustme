// Extracted from library/std/src/keyword_docs.rs:245
#![allow(unused)]
fn main() {
    // Print Odd numbers under 30 with unit <= 5
    'tens: for ten in 0..3 {
        '_units: for unit in 0..=9 {
            if unit % 2 == 0 {
                continue;
            }
            if unit > 5 {
                continue 'tens;
            }
            println!("{}", ten * 10 + unit);
        }
    }
}
