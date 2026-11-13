use std::ops::{Deref, Index};

struct Marker;
struct Record {
    pair: (u32, u32),
}
static RECORD: Record = Record { pair: (17, 25) };

impl Deref for Marker {
    type Target = Record;
    fn deref(&self) -> &Record {
        &RECORD
    }
}

impl Index<usize> for Marker {
    type Output = Record;
    fn index(&self, _: usize) -> &Record {
        &RECORD
    }
}

fn field_through_trait<T: Deref<Target = Record>>(value: &T) -> u32 {
    value.pair.0 + value.pair.1
}

fn main() {
    let marker = Marker;
    assert_eq!(marker.pair.0, 17);
    assert_eq!((*marker).pair.1, 25);
    assert_eq!(marker[0].pair.0, 17);
    assert_eq!(field_through_trait(&marker), 42);
}
