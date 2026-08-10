// Extracted from library/core/src/result.rs:48
#![allow(unused)]
fn main() {
    // The `is_ok` and `is_err` methods do what they say.
    let good_result: Result<i32, i32> = Ok(10);
    let bad_result: Result<i32, i32> = Err(10);
    assert!(good_result.is_ok() && !good_result.is_err());
    assert!(bad_result.is_err() && !bad_result.is_ok());

    // `map` and `map_err` consume the `Result` and produce another.
    let good_result: Result<i32, i32> = good_result.map(|i| i + 1);
    let bad_result: Result<i32, i32> = bad_result.map_err(|i| i - 1);
    assert_eq!(good_result, Ok(11));
    assert_eq!(bad_result, Err(9));

    // Use `and_then` to continue the computation.
    let good_result: Result<bool, i32> = good_result.and_then(|i| Ok(i == 11));
    assert_eq!(good_result, Ok(true));

    // Use `or_else` to handle the error.
    let bad_result: Result<i32, i32> = bad_result.or_else(|i| Ok(i + 20));
    assert_eq!(bad_result, Ok(29));

    // Consume the result and return the contents with `unwrap`.
    let final_awesome_result = good_result.unwrap();
    assert!(final_awesome_result)
}
