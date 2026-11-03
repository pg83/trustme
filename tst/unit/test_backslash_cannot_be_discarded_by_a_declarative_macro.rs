//@ compile-fail: Unknown start of token
macro_rules! discard { ($($token:tt)*) => { 29u32 }; }

fn main() {
    assert_eq!(discard!(\), 29);
}
