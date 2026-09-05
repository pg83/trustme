// `i32: TryFrom<i32>` and `i32: TryFrom<u8>` have no impls written for them -
// they come from the blanket `impl<T, U: Into<T>> TryFrom<U> for T`, whose
// `Error` is `Infallible`.  Answering the projection from a lossy `TryFrom`
// impl that does not apply gave the call one error type and its own body
// another, and the emitted code would not compile.

use std::convert::Infallible;

fn main() {
    let same: Result<i32, Infallible> = i32::try_from(3);
    assert_eq!(same.unwrap(), 3);

    let widened: Result<i32, Infallible> = i32::try_from(7u8);
    assert_eq!(widened.unwrap(), 7);

    let narrowed = i32::try_from(1_000_000_000_000i64);
    assert!(narrowed.is_err());
}
