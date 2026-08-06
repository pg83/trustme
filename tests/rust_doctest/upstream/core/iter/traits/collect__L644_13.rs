// Extracted from library/core/src/iter/traits/collect.rs:644
fn main() -> Result<(), core::num::ParseIntError> {
let string = "1,2,123,4";

// Example given for a 2-tuple, but 1- through 12-tuples are supported
let (numbers, lengths): (Vec<_>, Vec<_>) = string
    .split(',')
    .map(|s| s.parse().map(|n: u32| (n, s.len())))
    .collect::<Result<_, _>>()?;

assert_eq!(numbers, [1, 2, 123, 4]);
assert_eq!(lengths, [1, 1, 3, 1]);
Ok(()) }
