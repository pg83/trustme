// A return-position `impl Trait` in a trait becomes an associated type, and a
// nested one is its own associated type rather than an erased type belonging to
// no function.
use std::fmt::Debug;
use std::ops::Deref;

trait Nested {
    fn pair(&self) -> impl Deref<Target = impl Debug + ?Sized>;
    fn deep(&self) -> impl Iterator<Item = impl Iterator<Item = impl Debug>>;
}

struct Text;

impl Nested for Text {
    #[allow(refining_impl_trait)]
    fn pair(&self) -> &'static str {
        "nested"
    }

    fn deep(&self) -> impl Iterator<Item = impl Iterator<Item = impl Debug>> {
        (0..2).map(|row| (0..2).map(move |column| row * 2 + column))
    }
}

fn main() {
    assert_eq!(format!("{:?}", &*Text.pair()), "\"nested\"");

    let mut seen = Vec::new();
    for row in Text.deep() {
        for cell in row {
            seen.push(format!("{:?}", cell));
        }
    }
    assert_eq!(seen, ["0", "1", "2", "3"]);
}
