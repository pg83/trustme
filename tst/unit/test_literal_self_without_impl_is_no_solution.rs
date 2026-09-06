//@ run-pass
// `NonZero::<u8>::try_from(5)`: the argument is an integer variable, and the
// blanket `impl<T, U: Into<T>> TryFrom<U> for T` asks `{integer}: Into<NonZero<u8>>`.
// Upstream matches impls against an integer variable like any other type - it is
// not a type variable, so nothing is "ambiguous outright" - and with no impl of
// `From<{integer}>` for `NonZero<u8>` the blanket candidate is unimplemented,
// leaving `TryFrom<u8>` alone to decide the literal.
use std::num::NonZero;

fn main() {
    assert_eq!(NonZero::<u8>::try_from(5), Ok(NonZero::new(5).unwrap()));
    assert_eq!(NonZero::<u32>::try_from(5), Ok(NonZero::new(5).unwrap()));
    assert_eq!(NonZero::<i32>::try_from(-5), Ok(NonZero::new(-5).unwrap()));
    assert!(NonZero::<u8>::try_from(0).is_err());
}
