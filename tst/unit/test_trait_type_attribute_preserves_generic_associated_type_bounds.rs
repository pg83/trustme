//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Items {
    #[edits::passthrough]
    type Item<'a>: Clone where Self: 'a;
    fn copy<'a>(value: &Self::Item<'a>) -> Self::Item<'a> where Self: 'a { value.clone() }
}

struct Value;
impl Items for Value { type Item<'a> = &'a u16; }

fn main() {
    let value = 151u16;
    assert_eq!(*Value::copy(&&value), 151);
}
