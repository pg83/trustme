use std::mem;

enum NonZeroDiscriminant {
    One = 1,
    Two = 2,
    Three = 3,
}

fn main() {
    assert_eq!(mem::size_of::<NonZeroDiscriminant>(), 1);
    assert_eq!(mem::size_of::<Option<NonZeroDiscriminant>>(), 1);
    assert_eq!(mem::size_of::<Result<NonZeroDiscriminant, ()>>(), 1);

    assert!(None::<NonZeroDiscriminant>.is_none());
    assert!(matches!(Some(NonZeroDiscriminant::Two), Some(NonZeroDiscriminant::Two)));
}
