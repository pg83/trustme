// An item nested in a method still reaches the `Self` constructor of the impl
// around it, though `Self` is not a type name there.
struct S0(usize);

impl S0 {
    fn make() -> usize {
        const C: S0 = Self(7);

        fn bar() -> S0 {
            Self(4)
        }

        C.0 + bar().0
    }
}

fn main() {
    assert_eq!(S0::make(), 11);
}
