#![feature(explicit_tail_calls)]
#![allow(incomplete_features, unreachable_code)]

fn destination(value: i32) -> i32 {
    value + 1000
}

fn exit_argument(value: i32) -> i32 {
    let pointer: fn(i32) -> i32 = destination;
    'argument: loop {
        become pointer(break 'argument value);
    }
}

fn main() {
    assert_eq!(exit_argument(37), 37);
}
