#![feature(box_syntax)]

use std::ops::Deref;

struct Holder<T> {
    value: T,
}

impl<T> Holder<T> {
    fn new(value: T) -> Self {
        Holder { value }
    }
}

impl<T> Deref for Holder<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.value
    }
}

fn main() {
    let construct = Holder::new;
    let holder = construct([13u16, 17, 19]);
    let middle = holder.value[1];
    let last = (*holder)[2];
    let sum = middle as u32 + last as u32;
    let repeated = [sum; 3];
    let owned = box Holder::new(repeated);
    let read = |index: usize| owned.value[index];
    assert_eq!(read(0), 36);
    assert_eq!(read(2), 36);
    let unit = {
        read(1);
    };
    assert_eq!(unit, ());
    assert_eq!(owned.len(), 3);
}
