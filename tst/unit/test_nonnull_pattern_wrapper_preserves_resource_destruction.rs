#![feature(pattern_types, pattern_type_macro)]

use std::sync::atomic::{AtomicUsize, Ordering};

static DROPPED: AtomicUsize = AtomicUsize::new(0);

#[repr(transparent)]
struct Resource(u8);

impl Drop for Resource {
    fn drop(&mut self) {
        DROPPED.fetch_add(self.0 as usize, Ordering::SeqCst);
    }
}

type Restricted = std::pat::pattern_type!(Resource is !null);

fn main() {
    let resource: Restricted = unsafe { std::mem::transmute(Resource(37)) };
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0);
    drop(resource);
    assert_eq!(DROPPED.load(Ordering::SeqCst), 37);
}
