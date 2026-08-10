// Extracted from library/core/src/result.rs:1388
#![allow(unused)]
fn main() {
    fn sq_then_to_string(x: u32) -> Result<String, &'static str> {
        x.checked_mul(x).map(|sq| sq.to_string()).ok_or("overflowed")
    }

    assert_eq!(Ok(2).and_then(sq_then_to_string), Ok(4.to_string()));
    assert_eq!(Ok(1_000_000).and_then(sq_then_to_string), Err("overflowed"));
    assert_eq!(Err("not a number").and_then(sq_then_to_string), Err("not a number"));
}
