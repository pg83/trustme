#![feature(yeet_expr, coroutines, explicit_tail_calls)]

macro_rules! expression_text {
    ($value:expr) => { stringify!($value) };
}

fn main() {
    assert_eq!(expression_text!(do yeet 7u8), "do yeet 7u8");
    assert_eq!(expression_text!(do yeet), "do yeet");
    assert_eq!(expression_text!(yield 9u16), "yield 9u16");
    let block = expression_text!({ become target(4); });
    let compact: String = block.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "{becometarget(4);}");
    assert_eq!(expression_text!(return 11u8), "return 11u8");
}
