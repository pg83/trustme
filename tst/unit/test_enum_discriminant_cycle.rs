//@ compile-fail: cycle detected
// Two variants whose expressions read each other never settle; rustc
// reports E0391, not values that happen to coincide.

#[repr(isize)]
enum E {
    A = E::B as isize,
    B = E::A as isize,
}

fn main() {
    let _ = E::A;
}
