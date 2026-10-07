#![feature(pattern_types, pattern_type_macro)]
#![allow(incomplete_features, internal_features)]

use std::mem::size_of;
use std::pat::pattern_type;

type NonNullPtr = pattern_type!(*const u8 is !null);

trait Kind {
    const NAME: &'static str;
}

impl Kind for *const u8 {
    const NAME: &'static str = "pointer";
}

impl Kind for NonNullPtr {
    const NAME: &'static str = "non-null pointer";
}

fn main() {
    assert_eq!(<*const u8 as Kind>::NAME, "pointer");
    assert_eq!(<NonNullPtr as Kind>::NAME, "non-null pointer");
    assert_eq!(size_of::<NonNullPtr>(), size_of::<*const u8>());
    assert_eq!(size_of::<Option<NonNullPtr>>(), size_of::<*const u8>());
}
