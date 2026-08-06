// Extracted from library/core/src/iter/mod.rs:174
#![allow(unused)]
fn main() {
    let values = vec![1, 2, 3, 4, 5];
    {
        let result = match IntoIterator::into_iter(values) {
            mut iter => loop {
                let next;
                match iter.next() {
                    Some(val) => next = val,
                    None => break,
                };
                let x = next;
                let () = { println!("{x}"); };
            },
        };
        result
    }
}
