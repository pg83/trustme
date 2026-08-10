// Extracted from library/core/src/iter/traits/iterator.rs:1992
#![allow(unused)]
fn main() {
    let results = [Ok(1), Err("nope"), Ok(3), Err("bad")];

    let result: Result<Vec<_>, &str> = results.into_iter().collect();

    // gives us the first error
    assert_eq!(Err("nope"), result);

    let results = [Ok(1), Ok(3)];

    let result: Result<Vec<_>, &str> = results.into_iter().collect();

    // gives us the list of answers
    assert_eq!(Ok(vec![1, 3]), result);
}
