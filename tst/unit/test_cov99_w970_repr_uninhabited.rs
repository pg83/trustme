#![allow(dead_code)]

enum Void {}

#[repr(u8)]
enum Tagged {
    Only(Void),
}

const TAGGED_SIZE: usize = std::mem::size_of::<Tagged>();

fn main() {
    assert_eq!(TAGGED_SIZE, 1);
    assert_eq!(std::mem::align_of::<Tagged>(), 1);
}
