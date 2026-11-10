//@ proc-macro-aux-build: own_default_assignment_observer.rs
#![feature(default_field_values)]

use own_default_assignment_observer::ObserveAssignment;

#[derive(ObserveAssignment)]
struct Values {
    sum: u16 = {
        let mut first = 3u16;
        let mut second = 5u16;
        (first, second) = (second + 36, first + 40);
        first + second
    },
}

fn main() {
    let value = Values { .. };
    assert_eq!(value.sum, 84);
}
