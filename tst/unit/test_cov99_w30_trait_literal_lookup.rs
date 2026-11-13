//@ compile-fail: associated type `Item` must be specified for trait object
trait Cov30 {
    type Item;
}

fn main() {
    let _ = Cov30 {};
}
