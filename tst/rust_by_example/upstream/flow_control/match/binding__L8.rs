// Extracted from src/flow_control/match/binding.md:8
// A function `age` which returns a `u32`.
fn age() -> u32 {
    15
}

fn main() {
    println!("Tell me what type of person you are");

    match age() {
        0             => println!("I haven't celebrated my first birthday yet"),
        // Could `match` 1 ..= 12 directly but then what age
        // would the child be?
        // Could `match` n and use an `if` guard, but would
        // not contribute to exhaustiveness checks.
        // (Although in this case that would not matter since
        // a "catch-all" pattern is present at the bottom)
        // Instead, bind to `n` for the sequence of 1 ..= 12.
        // Now the age can be reported.
        n @ 1  ..= 12 => println!("I'm a child of age {:?}", n),
        n @ 13 ..= 19 => println!("I'm a teen of age {:?}", n),
        // A similar binding can be done when matching several values.
        n @ (1 | 7 | 15 | 13) => println!("I'm a teen of age {:?}", n),
        // Nothing bound. Return the result.
        n             => println!("I'm an old person of age {:?}", n),
    }
}
