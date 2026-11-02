//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Values {
    #[edits::expand_to(
        #[cfg(any())]
        fn absent(&self) -> MissingType;
        #[inline(always)]
        #[allow(unused_variables)]
        fn value(&self, ignored: u16) -> u16 { 101 }
        #[edits::expand_to(fn nested(&self) -> u16 { 103 })]
        fn nested(&self) -> u16;
    )]
    fn replaced(&self) -> u16;
}

struct Value;
impl Values for Value {}

fn main() { assert_eq!(Value.value(1) + Value.nested(), 204); }
