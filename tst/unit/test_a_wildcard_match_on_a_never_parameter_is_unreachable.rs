#![feature(never_type)]

fn impossible(value: !) -> u8 {
    match value {
        _ => 7,
    }
}

fn main() {
    let pointer: fn(!) -> u8 = impossible;
    assert_ne!(pointer as usize, 0);
}
