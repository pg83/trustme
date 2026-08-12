use std::cell::Cell;
use std::marker::PhantomPinned;
use std::pin::{pin, Pin};

struct Guard<'a> {
    dropped: &'a Cell<bool>,
    _pinned: PhantomPinned,
}

impl Guard<'_> {
    fn check(self: Pin<&mut Self>) {
        if self.dropped.get() {
            std::process::abort();
        }
    }
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.dropped.set(true);
    }
}

fn main() {
    let dropped = Cell::new(false);
    {
        let mut value: Pin<&mut Guard<'_>> = pin!(Guard {
            dropped: &dropped,
            _pinned: PhantomPinned,
        });
        value.as_mut().check();
        drop(value);
        if dropped.get() {
            std::process::abort();
        }
    }
    if !dropped.get() {
        std::process::abort();
    }

    dropped.set(false);
    pin!(Guard {
        dropped: &dropped,
        _pinned: PhantomPinned,
    })
    .as_mut()
    .check();
    if !dropped.get() {
        std::process::abort();
    }

    let slice: Pin<&mut [PhantomPinned]> = pin!([PhantomPinned, PhantomPinned]);
    if slice.len() != 2 {
        std::process::abort();
    }
}
