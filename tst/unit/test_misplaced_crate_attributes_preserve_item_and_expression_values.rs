#![feature(no_core)]

struct Record {
    value: u16,
}

#[no_core]
fn value() -> u16 {
    13
}

fn main() {
    let record = Record {
        #[no_core]
        value: value(),
    };
    let result = match record.value {
        #[no_core]
        13 => 29u16,
        _ => 0,
    };
    assert_eq!(record.value, 13);
    assert_eq!(result, 29);
}
