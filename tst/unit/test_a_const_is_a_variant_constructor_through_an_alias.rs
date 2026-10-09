// coset 0.4: `const ISS: ClaimName = ClaimName::Assigned(..)` where
// `ClaimName` aliases a generic enum. The call through the alias is the
// whole initialiser, and it still becomes the variant's constructor.
#[derive(Debug, PartialEq)]
pub enum Label<T> {
    Private(i64),
    Assigned(T),
}

pub type ClaimName = Label<u8>;

const ONE: ClaimName = ClaimName::Assigned(1);

fn main() {
    assert_eq!(ONE, Label::Assigned(1));
    assert_ne!(ONE, Label::Private(1));
}
