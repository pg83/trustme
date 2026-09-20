// A generic constant's body belongs to the generic item: its declared type is
// `T`, and that is what the body is checked against, once. Only the evaluation
// that follows is instantiated. Reaching the constant first through an
// instantiated path must not hand the body `i32` as its expected type -- the
// module walk marks `CREATE` generic before the impl walk evaluates `MADE`,
// so this ordering is what a crate-wide walk always produces.

#![feature(generic_const_items, const_trait_impl)]
#![allow(incomplete_features)]

#[const_trait]
trait Create {
    fn create() -> Self;
}

impl const Create for i32 {
    fn create() -> i32 {
        4096
    }
}

const CREATE<T: const Create>: T = T::create();

struct Holder;

impl Holder {
    const MADE: i32 = CREATE::<i32>;
}

fn main() {
    assert_eq!(Holder::MADE, 4096);
}
