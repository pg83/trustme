#[path = "support/shebang_round265_empty.rs"]
mod empty;
//@ input: support/shebang_round265_empty.rs
//@ compile-variant: -O

struct Counter(usize);
impl Counter {
    fn advance(&mut self, step: usize) { self.0 += step; }
}
fn main() {
    let mut counter = Counter(7);
    counter.advance(5);
    assert_eq!(counter.0, 12);
}
