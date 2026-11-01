#![feature(negative_impls)]

trait Read { fn value(&self) -> u16; }
struct Item(u16);

#[cfg(any())]
unsafe impl !Read for Item {}

impl Read for Item {
    fn value(&self) -> u16 { self.0 }
}

fn read<T: Read>(value: &T) -> u16 { value.value() }

fn main() {
    assert_eq!(read(&Item(31)), 31);
}
