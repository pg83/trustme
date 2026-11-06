use std::num::NonZeroU8;

const fn make(present: bool) -> Option<[[NonZeroU8; 1]; 1]> {
    if present {
        Some([[unsafe { NonZeroU8::new_unchecked(83) }]])
    } else {
        None
    }
}

const fn read(value: Option<[[NonZeroU8; 1]; 1]>) -> u8 {
    match value {
        Some(bytes) => bytes[0][0].get(),
        None => 0,
    }
}

const EMPTY: Option<[[NonZeroU8; 1]; 1]> = make(false);
const PRESENT: Option<[[NonZeroU8; 1]; 1]> = make(true);
const VALUES: [u8; 2] = [read(EMPTY), read(PRESENT)];

fn main() {
    assert_eq!(VALUES, [0, 83]);
    assert_eq!(EMPTY.map(|bytes| bytes[0][0].get()), None);
    assert_eq!(PRESENT.map(|bytes| bytes[0][0].get()), Some(83));
}
