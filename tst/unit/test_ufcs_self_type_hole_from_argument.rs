// `<*const _>::from(&value)` leaves the pointee to the call. Only the reflexive
// `From<T> for T` applies, so the parameter is `*const _` and the argument's
// reference-to-pointer coercion says what the pointee is. Asking that candidate
// whether it can take the argument reduced to relating the pointer's target, an
// unfilled slot of the candidate's own head, with i32 - which was answered "not
// known" rather than "yes, by being i32", so the candidate was dropped and the
// pointee stayed unfilled.

fn main() {
    let value = 5i32;
    let pointer = <*const _>::from(&value);
    assert_eq!(unsafe { *pointer }, 5);
}
