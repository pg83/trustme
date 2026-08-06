// Extracted from library/core/src/any.rs:660
mod unique {
    use std::any::TypeId;
    use std::collections::BTreeSet;
    use std::marker::PhantomData;
    use std::sync::Mutex;

    static ID_SET: Mutex<BTreeSet<TypeId>> = Mutex::new(BTreeSet::new());

    // TypeId has only covariant uses, which makes Unique covariant over TypeAsId 🚨
    #[derive(Debug, PartialEq)]
    pub struct Unique<TypeAsId: 'static>(
        // private field prevents creation without `new` outside this module
        PhantomData<TypeAsId>,
    );

    impl<TypeAsId: 'static> Unique<TypeAsId> {
        pub fn new() -> Option<Self> {
            let mut set = ID_SET.lock().unwrap();
            (set.insert(TypeId::of::<TypeAsId>())).then(|| Self(PhantomData))
        }
    }

    impl<TypeAsId: 'static> Drop for Unique<TypeAsId> {
        fn drop(&mut self) {
            let mut set = ID_SET.lock().unwrap();
            (!set.remove(&TypeId::of::<TypeAsId>())).then(|| panic!("duplicity detected"));
        }
    }
}

use unique::Unique;

// `OtherRing` is a subtype of `TheOneRing`. Both are 'static, and thus have a TypeId.
type TheOneRing = fn(&'static ());
type OtherRing = fn(&());

fn main() {
    let the_one_ring: Unique<TheOneRing> = Unique::new().unwrap();
    assert_eq!(Unique::<TheOneRing>::new(), None);

    let other_ring: Unique<OtherRing> = Unique::new().unwrap();
    // Use that `Unique<OtherRing>` is a subtype of `Unique<TheOneRing>` 🚨
    let fake_one_ring: Unique<TheOneRing> = other_ring;
    assert_eq!(fake_one_ring, the_one_ring);

    std::mem::forget(fake_one_ring);
}
