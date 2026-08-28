#![feature(specialization)]
#![allow(incomplete_features)]

// A specialising impl inherits value items from the nearest shadowed impl.
// Static getValue must ask the solver for the provider instead of repeating
// the legacy impl walk for methods and associated constants.
trait Items {
    const VALUE: usize;
    fn inherited() -> usize;
    fn overridden() -> usize;
}

struct Wrapper<T>(T);

trait CloneOrCopy {
    fn selected() -> usize;
}

impl<T: Clone> CloneOrCopy for T {
    default fn selected() -> usize {
        3
    }
}

impl<T: Copy> CloneOrCopy for T {
    fn selected() -> usize {
        4
    }
}

#[derive(Clone)]
struct CloneOnly;

impl<T> Items for Wrapper<T> {
    default const VALUE: usize = 1;

    default fn inherited() -> usize {
        Self::VALUE
    }

    default fn overridden() -> usize {
        0
    }
}

impl Items for Wrapper<u8> {
    fn overridden() -> usize {
        2
    }
}

fn main() {
    assert_eq!(<Wrapper<u8> as Items>::VALUE, 1);
    assert_eq!(<Wrapper<u8> as Items>::inherited(), 1);
    assert_eq!(<Wrapper<u8> as Items>::overridden(), 2);
    // A dead specialising predicate must be NoSolution, not Ambiguous: the
    // static value-provider query must select the Clone fallback for a
    // concrete non-Copy type and the Copy provider only for a Copy type.
    assert_eq!(<CloneOnly as CloneOrCopy>::selected(), 3);
    assert_eq!(<u8 as CloneOrCopy>::selected(), 4);
}
