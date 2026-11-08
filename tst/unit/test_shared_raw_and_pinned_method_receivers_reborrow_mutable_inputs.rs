#![feature(arbitrary_self_types_pointers, pin_ergonomics)]

use std::pin::Pin;

struct Counter(u32);

impl Counter {
    unsafe fn raw_value(self: *const Self) -> u32 {
        (*self).0
    }

    fn pinned_value(self: Pin<&Self>) -> u32 {
        self.get_ref().0
    }
}

fn main() {
    let mut counter = Counter(41);
    let pointer: *mut Counter = &mut counter;
    assert_eq!(unsafe { pointer.raw_value() }, 41);
    unsafe { (*pointer).0 = 43; }
    assert_eq!(unsafe { pointer.raw_value() }, 43);

    let mut pinned = Pin::new(&mut counter);
    assert_eq!(pinned.as_mut().pinned_value(), 43);
    pinned.as_mut().get_mut().0 = 47;
    assert_eq!(pinned.as_mut().pinned_value(), 47);
}
