//@ proc-macro-aux-build: interner_growth_tokens.rs
use interner_growth_tokens::growth_tokens;
fn main() {
    let (first, text, last) = growth_tokens!();
    assert_eq!((first,last),(19,19));
    let mut words = text.split_whitespace();
    for index in 0..390_000 { assert_eq!(words.next().unwrap(),format!("n{:06}",index)); }
    assert_eq!(words.next(),None);
}
