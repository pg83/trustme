// An `impl Trait` associated type is defined by the impl's items whose own
// signature names it (upstream `opaque_types_defined_by` with
// `ImplTraitInAssocTypes`: `sig_types::walk_types` over the item's inputs,
// output or type), not by every item of the impl.  `foo` and `FOO` name no
// opaque, so they use `Self::Assoc` opaquely, and `Self::bar()` / `Self::BAR`
// simply have that type there.
//
// Same shape as the upstream test
// type-alias-impl-trait/impl_trait_in_trait_defined_outside_trait3.rs.
#![feature(impl_trait_in_assoc_type)]

trait Trait: Sized {
    type Assoc;
    fn foo() -> usize;
    fn bar() -> Self::Assoc;
}

impl Trait for () {
    type Assoc = impl std::fmt::Debug;
    fn foo() -> usize {
        let x: Self::Assoc = Self::bar();
        format!("{:?}", x).len()
    }
    fn bar() -> Self::Assoc {
        "hi"
    }
}

trait Trait2: Sized {
    type Assoc;
    const FOO: ();
    const BAR: Self::Assoc;
}

impl Trait2 for () {
    type Assoc = impl Copy;
    const FOO: () = {
        let x: Self::Assoc = Self::BAR;
        let _y = x;
    };
    const BAR: Self::Assoc = "";
}

fn main() {
    assert_eq!(<() as Trait>::foo(), 4);
    let _ = <() as Trait2>::FOO;
}
