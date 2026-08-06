// Extracted from library/core/src/iter/mod.rs:298
#![allow(unused)]
fn main() {
    let v = vec![1, 2, 3, 4, 5];
    
    v.iter().for_each(|x| println!("{x}"));
    // or
    for x in &v {
        println!("{x}");
    }
}
