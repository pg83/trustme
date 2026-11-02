//@ proc-macro-aux-build: trait_attribute_edits.rs
//@ proc-macro-aux-build: type_phase_identity.rs
extern crate trait_attribute_edits as edits;
extern crate type_phase_identity as tokens;

trait Values {
    tokens::emit! {
        fn first(&self) -> u16 { 173 }
        #[edits::expand_to(fn second(&self) -> u16 { 179 })]
        fn second(&self) -> u16;
    }
    fn last(&self) -> u16 { 181 }
}

struct Value;
impl Values for Value {}

fn main() { assert_eq!(Value.first() + Value.second() + Value.last(), 533); }
