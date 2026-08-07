use std::mem::size_of;

trait Arena {
    type Dyn: ?Sized;
}

struct Ref<A: Arena> {
    head: u8,
    tail: A::Dyn,
}

struct Obstack;

impl Arena for Obstack {
    type Dyn = [()];
}

fn main() {
    assert_eq!(size_of::<&Ref<Obstack>>(), 2 * size_of::<usize>());
}
