// `do yeet` with nothing after it yeets a unit, the same as `do yeet ()`.
#![feature(yeet_expr)]

fn always_yeet() -> Option<String> {
    do yeet;
}

fn yeet_value() -> Result<u32, i32> {
    do yeet 7;
}

fn main() {
    assert!(always_yeet().is_none());
    assert_eq!(yeet_value(), Err(7));
}
