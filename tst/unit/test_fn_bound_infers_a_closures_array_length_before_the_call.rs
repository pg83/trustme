#![feature(generic_arg_infer)]

fn apply<F: FnOnce([u8; 3]) -> u16>(function: F) -> u16 {
    function([3, 5, 7])
}

fn main() {
    let operation = |values: [u8; _]| {
        u16::from(values[0]) * u16::from(values[1]) + u16::from(values[2])
    };
    assert_eq!(apply(operation), 22);
}
