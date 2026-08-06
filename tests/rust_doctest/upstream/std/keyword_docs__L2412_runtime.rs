// Extracted from library/std/src/keyword_docs.rs:2412
#![allow(unused)]
fn main() {
    let mut counter = Some(0);
    
    while let Some(i) = counter {
        if i == 10 {
            counter = None;
        } else {
            println!("{i}");
            counter = Some (i + 1);
        }
    }
}
