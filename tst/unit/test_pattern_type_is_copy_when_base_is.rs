// A pattern type is `Copy` and `Clone` when its base type is (upstream
// `instantiate_constituent_tys_for_copy_clone_trait`: `ty::Pat(ty, ..)` has the
// constituent `ty`), so reading `self.0` through a shared reference copies it
// rather than moving out of the borrow.
//
// Same shape as the upstream test type/pattern_types/matching.rs.
#![feature(pattern_types, pattern_type_macro)]

use std::pat::pattern_type;

struct Thing(pattern_type!(u32 is 1..));

impl PartialEq for Thing {
    fn eq(&self, other: &Thing) -> bool {
        unsafe { std::mem::transmute::<_, u32>(self.0) == std::mem::transmute::<_, u32>(other.0) }
    }
}

fn main() {
    let a = Thing(2);
    let b = Thing(2);
    let c = Thing(3);
    assert!(a == b);
    assert!(!(a == c));
}
