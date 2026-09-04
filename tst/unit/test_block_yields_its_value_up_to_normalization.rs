// A block yields what its value yields, and a deref coercion moving into one
// checks that. One side may still hold a projection the other has had
// normalized - `&<usize as SliceIndex<[&str]>>::Output` against `&&str` - which
// is the same type written the other way, and calling them different aborted
// the compiler.

use std::ops::Index;

trait Context {
    type Container: ?Sized;

    fn first(container: &Self::Container) -> &str;
}

struct Holder;

impl Context for Holder {
    type Container = [&'static str];

    fn first(container: &Self::Container) -> &str {
        container.index(0)
    }
}

fn main() {
    assert_eq!(Holder::first(&["a", "b"][..]), "a");
}
