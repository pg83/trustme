// A method whose parameter is `T::Item`, with `T: Trait` supplied by the impl's
// where-clause, is called with `T` already known.  Upstream instantiates the
// method's signature and normalizes it before the arguments are related, so the
// parameter reads `u32`.  Relating `22` against the projection itself never
// settled: the call stayed ambiguous, its argument coercion was registered anew
// on every pass, and typecheck ran out of iterations.

use std::marker::PhantomData;

trait Trait {
    type Item;
}

struct Struct;

impl Trait for Struct {
    type Item = u32;
}

trait Method<T>
where
    T: Trait,
{
    fn method(&self, item: T::Item) -> u32;
}

struct Holder<T>(PhantomData<T>);

impl<T> Method<T> for Holder<T>
where
    T: Trait,
{
    fn method(&self, item: T::Item) -> u32 {
        let _ = item;
        7
    }
}

fn main() {
    let holder = Holder(PhantomData::<Struct>);
    assert_eq!(holder.method(22), 7);
}
