// A `where` clause may be written with no predicates at all, which is how
// `type Assoc where = ();` reads. The parser always expected a predicate, so the
// `=` right after `where` was an unexpected token.
//
// Same shape as the upstream test
// where-clauses/where-clause-placement-assoc-type-in-impl.rs.
#![allow(dead_code)]

trait Trait {
    type Assoc
    where
        u32: Copy;
    type Assoc2;
    type Assoc3;
}

impl Trait for u32 {
    // Empty clause before the aliased type.
    type Assoc where = ();
    // A clause before and after it.
    type Assoc2 where u32: Copy = () where i32: Copy;
    // The ordinary trailing placement.
    type Assoc3 = () where u32: Copy;
}

struct Plain
where
{
    a: u8,
}

fn takesNothing<T>() -> u8
where
{
    1
}

fn main() {
    let p = Plain { a: 2 };
    assert_eq!(p.a, 2);
    assert_eq!(takesNothing::<u32>(), 1);
}
