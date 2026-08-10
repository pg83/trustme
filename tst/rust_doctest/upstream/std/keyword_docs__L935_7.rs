// Extracted from library/std/src/keyword_docs.rs:935
#![allow(unused)]
fn main() {
    let opt = Option::None::<usize>;
    let x = match opt {
        Some(int) => int,
        None => 10,
    };
    assert_eq!(x, 10);

    let a_number = Option::Some(10);
    match a_number {
        Some(x) if x <= 5 => println!("0 to 5 num = {x}"),
        Some(x @ 6..=10) => println!("6 to 10 num = {x}"),
        None => panic!(),
        // all other numbers
        _ => panic!(),
    }
}
