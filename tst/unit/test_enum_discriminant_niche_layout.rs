use std::mem::size_of;

#[repr(u8)]
enum AlmostFull {
    First = 0,
    Last = 254,
}

enum OptionLike {
    Value(AlmostFull),
    Empty,
}

const _: () = assert!(size_of::<OptionLike>() == 1);

fn main() {}
