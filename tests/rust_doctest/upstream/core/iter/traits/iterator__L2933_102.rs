// Extracted from library/core/src/iter/traits/iterator.rs:2933
#![allow(unused)]
#![feature(try_find)]
fn main() {

    let a = ["1", "2", "lol", "NaN", "5"];

    let is_my_num = |s: &str, search: i32| -> Result<bool, std::num::ParseIntError> {
        Ok(s.parse::<i32>()? == search)
    };

    let result = a.into_iter().try_find(|&s| is_my_num(s, 2));
    assert_eq!(result, Ok(Some("2")));

    let result = a.into_iter().try_find(|&s| is_my_num(s, 5));
    assert!(result.is_err());
}
