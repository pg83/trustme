// Extracted from library/std/src/keyword_docs.rs:551
#![allow(unused)]
fn main() {
    for i in 0..5 {
        println!("{}", i * 2);
    }

    for i in std::iter::repeat(5) {
        println!("turns out {i} never stops being 5");
        break; // would loop forever otherwise
    }

    'outer: for x in 5..50 {
        for y in 0..10 {
            if x == y {
                break 'outer;
            }
        }
    }
}
