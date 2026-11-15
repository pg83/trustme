#![feature(explicit_tail_calls)]
#![allow(unreachable_code)]

fn target() -> u32 {
    0
}

fn return_while_building_callee() -> u32 {
    become ((return 42, target as fn() -> u32).1)()
}

fn main() {
    assert_eq!(return_while_building_callee(), 42);
}
