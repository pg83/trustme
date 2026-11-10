//@ proc-macro-aux-build: own_empty_use_text_roundtrip.rs

macro_rules! round_trip {
    ($item:item) => {
        own_empty_use_text_roundtrip::preserve!($item);
    };
}

round_trip! {
    use std::mem::size_of;
}

fn main() {
    assert_eq!(size_of::<[u16; 3]>(), 6);
}
