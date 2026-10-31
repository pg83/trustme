#![feature(type_alias_impl_trait, unboxed_closures)]

use std::sync::atomic::{AtomicUsize, Ordering};

static OBSERVED: AtomicUsize = AtomicUsize::new(0);

type Callback = impl FnOnce<(u16,)>;

struct Factory;

impl Factory {
    fn keep(&self, callback: Callback) -> Callback {
        callback
    }

    #[define_opaque(Callback)]
    fn make(&self) -> Callback {
        self.keep(|value: u16| {
            OBSERVED.store(usize::from(value), Ordering::SeqCst);
            (String::from("kept"), value)
        })
    }
}

fn main() {
    drop(Factory.make()(257));
    assert_eq!(OBSERVED.load(Ordering::SeqCst), 257);
}
