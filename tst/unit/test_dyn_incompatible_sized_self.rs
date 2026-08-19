//@ compile-fail: is not dyn compatible
// A trait bounded by `Sized` has no `dyn` form: a trait object points at an
// unsized value, so no impl of such a trait could ever be behind the pointer.
// The same clause on a method only keeps that method out of the vtable, which
// `test_dyn_sized_impl.rs` covers.

trait TraitWithSize
where
    Self: Sized,
{
}

struct S;

impl TraitWithSize for S {}

fn main() {
    let obj: Box<dyn TraitWithSize> = Box::new(S);
    let _ = obj;
}
