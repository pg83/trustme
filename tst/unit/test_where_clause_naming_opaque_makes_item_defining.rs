// `fn foo() -> Foo where Self::Assoc:` names the impl's associated-type opaque
// in its where-clause, and upstream's defining-use walk covers an item's
// predicates as well as its inputs and output (`sig_types::walk_types` visits
// `explicit_predicates_of`), so `foo` defines `Assoc` as `()`.
//
// Same shape as the upstream test
// type-alias-impl-trait/struct-assignment-validity.rs.
#![feature(impl_trait_in_assoc_type)]

struct Bar;

trait Trait {
    type Assoc;
    fn foo() -> Foo;
}

impl Trait for Bar {
    type Assoc = impl std::fmt::Debug;
    fn foo() -> Foo
    where
        Self::Assoc:,
    {
        let x: <Bar as Trait>::Assoc = ();
        Foo { field: x }
    }
}

struct Foo {
    field: <Bar as Trait>::Assoc,
}

fn main() {
    let f = <Bar as Trait>::foo();
    assert_eq!(format!("{:?}", f.field), "()");
}
