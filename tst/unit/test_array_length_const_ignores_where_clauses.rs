// The anonymous constant in an array length is its own item: it has the
// enclosing item's generics but none of its where-clauses (upstream's
// `gather_explicit_predicates_of` reads the predicates off the node's own
// generics, and an anonymous constant has none).  So `u8::ASSOC_CONST` in
// `[(T, U); u8::ASSOC_CONST]` sees only the impl `Trait<()> for u8` and is
// resolved, while the same path in the function body would be ambiguous
// between `u8: Trait<T>` and `u8: Trait<U>`.

trait Trait<T> {
    const ASSOC_CONST: usize = 0;
}

impl Trait<()> for u8 {}

fn foo<T, U>() -> [(T, U); u8::ASSOC_CONST]
where
    u8: Trait<T> + Trait<U>,
{
    []
}

fn main() {
    let x: [((), ()); 0] = foo::<(), ()>();
    assert_eq!(x.len(), 0);
}
