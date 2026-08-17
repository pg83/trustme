// A match always selects an arm, so a match whose every arm diverges diverges
// too, and so does the block holding it. Divergence coerces to any type, which
// is what lets these stand where a `bool` and a `Glfw` are wanted.
#![allow(dead_code)]

struct Glfw;

fn condition() {
    if { if true { return; } else { return; }; } {}
}

fn value() -> Glfw {
    if true {
        return Glfw;
    } else {
        panic!()
    };
}

fn main() {
    let _ = condition;
    let _ = value;
}
