// `Ptr::Target::eq(self, other)` in `impl PartialEq<P<Q>> for P<Ptr> where
// Ptr::Target: PartialEq<Q::Target>`: the callee is `<Ptr::Target as
// PartialEq<?Rhs>>::eq`, and upstream resolves the callee's pending bound before
// coercing an argument whose parameter still has a variable
// (`resolve_vars_with_obligations` in `check_argument_types`), so the where-clause
// fixes `Rhs = Q::Target` and `other: &P<Q>` then derefs into `&Q::Target`;
// binding `Rhs` from the argument first would ask for `PartialEq<P<Q>>`.
//
// Same shape as `impl PartialEq<Pin<Q>> for Pin<Ptr>` in core.
use std::ops::Deref;

struct P<Ptr>(Ptr);

impl<Ptr: Deref> Deref for P<Ptr> {
    type Target = Ptr::Target;
    fn deref(&self) -> &Ptr::Target {
        &*self.0
    }
}

impl<Ptr: Deref, Q: Deref> PartialEq<P<Q>> for P<Ptr>
where
    Ptr::Target: PartialEq<Q::Target>,
{
    fn eq(&self, other: &P<Q>) -> bool {
        Ptr::Target::eq(self, other)
    }
}

fn main() {
    let a = P(Box::new(5));
    let b = P(&5);
    let c = P(&6);
    assert!(a == b);
    assert!(!(a == c));
}
