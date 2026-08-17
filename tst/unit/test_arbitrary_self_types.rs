// A receiver need not name `Self`: it may reach it through its own `Receiver`
// impl, and it may be a type parameter bounded to deref to `Self`.
#![feature(arbitrary_self_types)]

struct Counter(u32);

struct Handle(Counter);

impl core::ops::Receiver for Handle {
    type Target = Counter;
}

struct Wrapper;

impl core::ops::Deref for Wrapper {
    type Target = Counter;

    fn deref(&self) -> &Counter {
        &Counter(7)
    }
}

impl Counter {
    fn through_receiver(self: Handle) -> u32 {
        self.0.0
    }

    fn through_deref(self: Wrapper) -> u32 {
        3
    }

    fn borrowed(&self) -> u32 {
        self.0
    }
}

/// A receiver that is a type parameter, which only its bound makes a receiver.
trait Bounded<T: core::ops::Deref<Target = Self>> {
    fn generic_receiver(self: T) -> u32;
    fn borrowed_generic_receiver(self: &T) -> u32;
}

fn main() {
    assert_eq!(Handle(Counter(5)).through_receiver(), 5);
    assert_eq!(Wrapper.through_deref(), 3);
    assert_eq!(Wrapper.borrowed(), 7);
    assert_eq!(Box::new(Counter(9)).borrowed(), 9);
}
