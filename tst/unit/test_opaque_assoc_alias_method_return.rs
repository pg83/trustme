//@ crate-type: lib

#![feature(impl_trait_in_assoc_type)]

trait Bound {}

struct Hidden;

impl Bound for Hidden {}

trait Trait {
    type Assoc: Bound;

    fn make() -> Self::Assoc;
}

struct Implementation;

impl Trait for Implementation {
    type Assoc = impl Bound;

    fn make() -> Self::Assoc {
        Hidden
    }
}
