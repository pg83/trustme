#![allow(unused_attributes)]

struct Record {
    value: u8,
}

fn main() {
    let mut destination = 0;
    Record {
        #[ignore]
        value: destination,
        ..
    } = Record { value: 53 };
    assert_eq!(destination, 53);
}
