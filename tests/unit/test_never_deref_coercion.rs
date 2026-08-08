#![feature(never_type)]

fn bool_from_never(value: &!) -> bool {
    *value
}

fn usize_from_never(value: &!) -> usize {
    *value
}

fn main() {}
