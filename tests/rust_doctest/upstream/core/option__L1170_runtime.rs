// Extracted from library/core/src/option.rs:1170
#![allow(unused)]
fn main() {
    let list = vec![1, 2, 3];

    // prints "got: 2"
    let x = list
        .get(1)
        .inspect(|x| println!("got: {x}"))
        .expect("list should be long enough");

    // prints nothing
    list.get(5).inspect(|x| println!("got: {x}"));
}
