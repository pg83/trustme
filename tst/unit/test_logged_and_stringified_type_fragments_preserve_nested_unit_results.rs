//@ compile-variant: -O
#![feature(log_syntax)]
macro_rules! inspect_type {
    ($ty:ty) => {{ log_syntax!($ty); stringify!($ty) }};
}
fn main() {
    let empty = inspect_type!(());
    assert_eq!(empty, "()");
    let function = inspect_type!(fn(fn() -> (), [u8; 3]) -> ());
    let compact: String = function.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "fn(fn()->(),[u8;3])->()");
}
