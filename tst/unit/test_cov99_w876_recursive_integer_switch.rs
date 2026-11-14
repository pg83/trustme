//@ compile-flags: -Zdump-mir -Zmir-opt-level=2

fn recursive_integer_switch(value: u8) -> u32 {
    match value {
        0 => 7,
        1 => 11,
        _ => recursive_integer_switch(0),
    }
}

fn main() {
    assert_eq!(recursive_integer_switch(2), 7);
}
