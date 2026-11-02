//@ compile-variant: -O
macro_rules! pattern_text { ($pattern:pat) => { stringify!($pattern) }; }
fn main() {
    let text = pattern_text!(-0.5);
    let compact: String = text.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "-0.5");
    let zero = pattern_text!(-0.0);
    let compact: String = zero.chars().filter(|c| !c.is_whitespace()).collect();
    assert_eq!(compact, "-0.0");
}
