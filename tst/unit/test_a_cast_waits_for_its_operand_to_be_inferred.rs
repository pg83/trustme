// `Pin::new(&mut value) as Pin<&mut dyn Trait>`: upstream checks a cast
// after the rest of the body's obligations (`check_casts`), so `Pin::new`'s
// pointer is already `&mut i64` from its argument and the cast is an
// unsizing coercion. Coercing into the cast's type while the operand was
// still `Pin<_>` made the pointer `&mut dyn Trait`, and `dyn Trait: Unpin`
// does not hold.
use std::pin::Pin;

trait Trait {
    fn get(&self) -> i64;
}

impl Trait for i64 {
    fn get(&self) -> i64 {
        *self
    }
}

fn main() {
    let mut value = 3i64;
    let pinned = Pin::new(&mut value) as Pin<&mut dyn Trait>;
    assert_eq!(pinned.get(), 3);
}
