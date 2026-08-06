// Extracted from library/std/src/keyword_docs.rs:585
#![allow(unused)]
fn main() {
    fn code() { }
    let iterator = 0..2;
    {
        let result = match IntoIterator::into_iter(iterator) {
            mut iter => loop {
                match iter.next() {
                    None => break,
                    Some(loop_variable) => { code(); },
                };
            },
        };
        result
    }
}
