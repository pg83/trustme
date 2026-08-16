// A type variable that is only ever constrained by diverging expressions falls
// back to `()` before edition 2024, not to `!`. Picking `!` makes the pending
// trait bounds fail, e.g. "Failed to find an impl of Default for !".
//
// Same shapes as rustc's check-pass ui tests never_type/impl_trait_fallback.rs
// and never_type/dependency-on-fallback-to-unit.rs.
trait Marker {}

impl Marker for () {}

// The hidden type of the opaque comes only from a diverging expression.
fn opaque_from_diverging() -> impl Marker {
    panic!()
}

// `<_>::default()` in an arm whose sibling diverges.
fn default_from_diverging() {
    match true {
        false => <_>::default(),
        true => return,
    }
}

fn deserialize<T: Default>() -> Result<T, ()> {
    Ok(T::default())
}

// The `?` discards the `Ok` type, leaving it unconstrained.
fn question_mark() -> Result<(), ()> {
    deserialize()?;
    Ok(())
}

fn main() {
    default_from_diverging();
    assert!(question_mark().is_ok());
    // `opaque_from_diverging` only has to compile; calling it would panic.
    let _ = opaque_from_diverging;
}
