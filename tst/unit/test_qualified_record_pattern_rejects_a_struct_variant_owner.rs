//@ compile-fail: Expeted enum path in pattern binding
#![feature(more_qualified_paths)]

struct Record {
    value: u8,
}

fn main() {
    match (Record { value: 191 }) {
        <Record>::Missing { .. } => {}
    }
}
