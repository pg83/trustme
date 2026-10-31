//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::echo;
macro_rules! inner { ($value:expr) => { echo!($value) }; }
macro_rules! outer { ($value:expr) => { inner!($value) }; }
macro_rules! ty { ($value:ty) => { echo!($value) }; }
macro_rules! path { ($value:path) => { echo!($value) }; }
macro_rules! pattern { ($value:pat) => { echo!($value) }; }
type Text = ty!(&'static str);
fn main() {
    assert_eq!(outer!(2 + 3) * 4, 20);
    assert_eq!(outer!((2 + 3) * 4), 20);
    let text: Text = "borrowed";
    assert_eq!(text, "borrowed");
    assert_eq!(path!(std::primitive::u16::MAX), 65535);
    let accepted = match 5 { pattern!(2 | 5) => true, _ => false };
    assert!(accepted);
}
