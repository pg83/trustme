// `default impl` marks every item in the block as specialisable, the same as
// writing `default` on each one. Only the per-item spelling was parsed, so the
// block form was an unexpected token at item level.
//
// Same shape as the upstream tests specialization/defaultimpl/projection.rs
// and coherence/coherence-doesnt-use-infcx-evaluate.rs.
#![feature(specialization)]
#![allow(incomplete_features)]

trait Bar {
    type Assoc;
    fn get(&self) -> u32;
}

default impl<T: Copy> Bar for T {
    type Assoc = u32;
    fn get(&self) -> u32 {
        1
    }
}

// The per-item spelling keeps working next to it.
trait Baz {
    fn baz(&self) -> u32;
}

struct S;

impl Baz for S {
    default fn baz(&self) -> u32 {
        2
    }
}

fn main() {
    assert_eq!(S.baz(), 2);
}
