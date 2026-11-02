//@ compile-variant: -O
macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }
fn compact(text: &str) -> String { text.chars().filter(|c| !c.is_whitespace()).collect() }
fn main() {
    assert_eq!(spelling!(()), "()");
    assert_eq!(compact(spelling!(fn((), ((),)) -> ())), "fn((),((),))->()");
    assert_eq!(compact(spelling!([(); 3])), "[();3]");
}
