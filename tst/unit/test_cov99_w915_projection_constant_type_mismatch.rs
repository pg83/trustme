//@ compile-fail: Type mismatch

trait Project {
    type Item;
}

struct Seed;
struct Local;

impl Project for Seed {
    type Item = Local;
}

impl <Seed as Project>::Item {
    const VALUE: u8 = 37;
}

fn main() {
    let _: bool = Local::VALUE;
}
