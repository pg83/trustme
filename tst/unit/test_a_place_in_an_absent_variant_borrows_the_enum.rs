// ron matches `Result<Untagged, TypeIdError>` where both variants of
// `Untagged` hold an empty enum: `Untagged` has no present variant, so `Ok`
// is absent and the enum is laid out as `Err` alone. The `Ok(Untagged::A(v)
// | Untagged::B(v))` arm still borrows `v`, a zero-sized place inside a
// variant that has no storage; its address is the enum's, where upstream
// puts every absent variant.
#![allow(dead_code)]

enum A {}
type B = A;

enum Untagged {
    A(A),
    B(B),
}

struct Error(u64);

#[inline(never)]
fn make(x: u64) -> Result<Untagged, Error> {
    Err(Error(x))
}

#[inline(never)]
fn get(x: u64) -> u64 {
    match make(x) {
        Ok(Untagged::A(void) | Untagged::B(void)) => match void {},
        Err(Error(v)) => v,
    }
}

fn main() {
    assert_eq!(get(7), 7);
    assert_eq!(std::mem::size_of::<Result<Untagged, Error>>(), 8);
}
