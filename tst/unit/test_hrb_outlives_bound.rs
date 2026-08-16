// `for<'a> T: 'a` quantifies an outlives bound over a lifetime that exists only
// inside the predicate. The binder was parsed and then discarded, which left the
// lifetime unresolved.
//
// Same shape as the upstream tests generic-associated-types/issue-86483.rs and
// higher-ranked/trait-bounds/issue-88586-hr-self-outlives-in-trait-def.rs.
pub trait Items<T>
where
    for<'a> T: 'a,
{
    type Item<'v>: IntoIterator<Item = &'v T>;
}

pub struct Holder<T>(T);

impl<T> Holder<T>
where
    for<'a> T: 'a,
{
    fn get(&self) -> &T {
        &self.0
    }
}

// An ordinary outlives bound in the same clause still applies.
fn borrow<'b, T>(v: &'b T) -> &'b T
where
    T: 'b,
{
    v
}

fn main() {
    let h = Holder(5u32);
    assert_eq!(*h.get(), 5);
    assert_eq!(*borrow(&6u32), 6);
}
