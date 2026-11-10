//@ edition: 2021
//@ compile-fail: error

struct Record {
    value: u8,
}

macro_rules! record {
    ($kind:path) => { $kind { value: 19 } };
}

fn main() {
    assert_eq!(record!(Record).value, 19);
}
