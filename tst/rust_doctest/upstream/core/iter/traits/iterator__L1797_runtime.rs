// Extracted from library/core/src/iter/traits/iterator.rs:1797
#![allow(unused)]
fn main() {
    let a = [1, 4, 2, 3];

    // this iterator sequence is complex.
    let sum = a.iter()
        .cloned()
        .filter(|x| x % 2 == 0)
        .fold(0, |sum, i| sum + i);

    println!("{sum}");

    // let's add some inspect() calls to investigate what's happening
    let sum = a.iter()
        .cloned()
        .inspect(|x| println!("about to filter: {x}"))
        .filter(|x| x % 2 == 0)
        .inspect(|x| println!("made it through filter: {x}"))
        .fold(0, |sum, i| sum + i);

    println!("{sum}");
}
