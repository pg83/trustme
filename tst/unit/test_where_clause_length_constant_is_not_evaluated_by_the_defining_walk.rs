/* rustc `opaque_types_defined_by` walks an item's predicates structurally: it
   evaluates no constant.  `where [u8; Self::ASSOC_C]:` names the length's anonymous
   constant, whose own predicates under `generic_const_exprs` are the method's and
   name it again; evaluating the length while walking them re-entered the constant
   ("loop in constant evaluation"). */
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Foo {
    const ASSOC_C: usize;
    fn foo()
    where
        [(); Self::ASSOC_C]:;
}

struct Bar;

impl Foo for Bar {
    const ASSOC_C: usize = 3;

    fn foo()
    where
        [u8; Self::ASSOC_C]:,
    {
        let _: [u8; Self::ASSOC_C] = loop {};
    }
}

fn main() {}
