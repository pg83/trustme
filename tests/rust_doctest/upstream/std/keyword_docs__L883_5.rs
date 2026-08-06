// Extracted from library/std/src/keyword_docs.rs:883
#![allow(unused)]
fn main() {
    loop {
        println!("hello world forever!");
        break;
    }
    
    let mut i = 1;
    loop {
        println!("i is {i}");
        if i > 100 {
            break;
        }
        i *= 2;
    }
    assert_eq!(i, 128);
}
