// `NonZero::new` is a transmute onto the niche of `Option<NonZero<T>>`, so
// reading that niche has to test the whole value. For an emulated 128-bit
// scalar the generated check looked at one half only, and every non-zero
// value whose high half was clear read back as `None`.
use core::num::NonZero;

fn main() {
    // The value that used to be misread: low half set, high half clear.
    assert!(NonZero::<u128>::new(1).is_some());
    assert!(NonZero::<i128>::new(1).is_some());
    assert_eq!(NonZero::<u128>::new(1 << 2).unwrap().trailing_zeros(), 2);

    // The other half, and both.
    assert!(NonZero::<u128>::new(1 << 100).is_some());
    assert!(NonZero::<u128>::new((1 << 100) | 1).is_some());

    // Zero is still the niche.
    assert!(NonZero::<u128>::new(0).is_none());
    assert!(NonZero::<i128>::new(0).is_none());

    // And the layout the transmute relies on holds.
    assert_eq!(
        core::mem::size_of::<Option<NonZero<u128>>>(),
        core::mem::size_of::<u128>()
    );
}
