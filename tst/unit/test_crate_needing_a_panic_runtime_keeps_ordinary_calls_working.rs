#![feature(panic_runtime)]
#![needs_panic_runtime]

fn adjust(value: u16) -> u16 {
    value * 3 + 2
}

fn main() {
    assert_eq!(adjust(17), 53);
}
