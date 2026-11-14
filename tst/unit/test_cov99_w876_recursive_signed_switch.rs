//@ compile-flags: -Zdump-mir -Zmir-opt-level=3

fn recursive_signed_switch(value: i32) -> i32 {
    match value {
        -1 => 19,
        0 => 23,
        _ => recursive_signed_switch(0),
    }
}

fn main() {
    assert_eq!(recursive_signed_switch(1), 23);
}
