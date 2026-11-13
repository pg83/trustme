//@ compile-fail: VALUE
trait Supply {
    const VALUE: u16;
}

struct Missing;

impl Supply for Missing {}

fn main() {
    let _ = <Missing as Supply>::VALUE;
}
