#![expect(incomplete_features)]
#![feature(explicit_tail_calls)]

macro_rules! call {
    ($function:expr $(, $argument:expr)*) => {
        ($function)($($argument),*)
    };
}

fn finish(value: u32) -> u32 {
    value
}

fn through_macro(value: u32) -> u32 {
    become call!(finish, value)
}

fn main() {
    assert_eq!(through_macro(42), 42);
}
