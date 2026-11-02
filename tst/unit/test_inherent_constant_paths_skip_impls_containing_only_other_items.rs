//@ compile-variant: -O

struct Source;
impl Source { fn method(value: u16) -> u16 { value + 7 } }
impl Source { const OTHER: u16 = 31; }
impl Source { const VALUE: u16 = 47; }

fn main() {
    let call: fn(u16) -> u16 = Source::method;
    assert_eq!(Source::VALUE, 47);
    assert_eq!(Source::OTHER, 31);
    assert_eq!(call(11), 18);
}
