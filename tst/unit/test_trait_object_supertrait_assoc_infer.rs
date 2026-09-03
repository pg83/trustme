// An associated type named in a trait object's binding may be declared by a
// supertrait rather than by the principal trait. Proving the unsizing
// coercion has to export the equality it found for that binding, not just the
// fact that the supertrait obligation holds, or the `_` is left unresolved.

trait Restriction {
    type Inner;

    fn inner(&self) -> Self::Inner;
}

trait Database: Restriction<Inner = u32> {}

struct Test;

impl Restriction for Test {
    type Inner = u32;

    fn inner(&self) -> u32 {
        42
    }
}

impl Database for Test {}

fn main() {
    let t = Test;
    let inferred: &dyn Database<Inner = _> = &t;
    assert_eq!(inferred.inner(), 42);

    let spelled: &dyn Database<Inner = u32> = &t;
    assert_eq!(spelled.inner(), 42);
}
