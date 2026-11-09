//@ compile-fail: MaybeTrait on unknown trait
trait Container {
    type Item: ?Clone;
}

fn main() {}
