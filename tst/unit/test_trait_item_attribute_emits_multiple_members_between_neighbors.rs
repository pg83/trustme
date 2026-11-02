//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Values {
    fn before(&self) -> u16 { 2 }
    #[edits::expand_to(
        fn first(&self) -> u16 { 3 }
        const MIDDLE: u16 = 5;
        fn second(&self) -> u16 { 7 }
        fn third(&self) -> u16 { 11 }
    )]
    fn replaced(&self) -> u16;
    fn after(&self) -> u16 { 13 }
}

struct Value;
impl Values for Value {}

fn main() {
    assert_eq!(Value.before() + Value.first() + Value.second() + Value.third() + Value.after(), 36);
    assert_eq!(Value::MIDDLE, 5);
}
