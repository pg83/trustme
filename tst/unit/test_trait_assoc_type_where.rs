// A trait's associated type may carry a where clause on either side of its
// default, the same way an impl's may. Only the trailing placement was parsed,
// so the `=` after a leading clause was an unexpected token.
//
// Same shape as the upstream test
// where-clauses/where-clause-placement-assoc-type-in-trait.rs.
#![allow(dead_code)]
#![feature(associated_type_defaults)]

trait Trait {
    // Leading clause.
    type Assoc where u32: Copy = ();
    // A clause on both sides.
    type Assoc2 where u32: Copy = () where i32: Copy;
    // The ordinary trailing placement.
    type Assoc3 = () where u32: Copy;
    // An empty leading clause.
    type Assoc4 where = ();
    // A bound alongside the clause.
    type Assoc5: Copy where u32: Copy = ();
}

struct S;

impl Trait for S {
    type Assoc = ();
    type Assoc2 = ();
    type Assoc3 = ();
    type Assoc4 = ();
    type Assoc5 = ();
}

fn main() {
    let _: <S as Trait>::Assoc = ();
    let _: <S as Trait>::Assoc5 = ();
}
