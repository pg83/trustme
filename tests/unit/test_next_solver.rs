//@ compile-flags: -Znext-solver

use std::any::Any;

trait Mirror {
    type Item;
}

struct Wrapper<T>(T);

impl<T> Mirror for Wrapper<T> {
    type Item = T;
}

fn infer_from_canonical_response<T>()
where
    Wrapper<T>: Mirror<Item = i32>,
{
}

trait Project {
    type Item;
}

impl<T> Project for T {
    type Item = u8;
}

// Structural auto-trait goals contain ten identical subgoals at every level.
// A canonical table must evaluate each unique `Send` goal once, rather than
// expanding the proof tree exponentially.
struct SendLeaf(*mut ());

unsafe impl Send for SendLeaf where SendLeaf: 'static {}

macro_rules! ten_fields {
    ($name:ident, $field:ty) => {
        struct $name(
            $field, $field, $field, $field, $field,
            $field, $field, $field, $field, $field,
        );
    };
}

ten_fields!(Send1, SendLeaf);
ten_fields!(Send2, Send1);
ten_fields!(Send3, Send2);
ten_fields!(Send4, Send3);
ten_fields!(Send5, Send4);
ten_fields!(Send6, Send5);
ten_fields!(Send7, Send6);
ten_fields!(Send8, Send7);

// The explicit environment candidate is authoritative while checking a
// generic body.  The blanket impl is also structurally applicable, but must
// not replace the projection equality supplied by the caller's environment.
fn prefer_param_env<T: Project<Item = u16>>(value: T::Item) -> u16 {
    value
}

fn main() {
    infer_from_canonical_response::<_>();

    // Both Vec<T>: Extend<T> and the specialised Vec<T>: Extend<&T> remain
    // possible until the map closure's output is known.  An ambiguous
    // specialised where-clause must not discard the fallback impl.
    let mut mapped: Vec<_> = vec![];
    mapped.extend(Some(1i32).into_iter().map(|value| value));
    assert_eq!(mapped, vec![1]);

    // The builtin `T -> dyn Trait` candidate is a conjunction of the
    // principal trait and every associated-type equality.  Its canonical
    // response must feed `Item = ()` back into `empty::<_>()`.
    let _: Box<dyn Iterator<Item = ()>> = Box::new(std::iter::empty());

    fn needs_send<T: Send>() {}
    needs_send::<Send8>();

    // `dyn Any` has both the builtin object candidate and the blanket Any
    // impl.  Candidate assembly must retain both and prefer the builtin one.
    let erased: &dyn Any = &37usize;
    assert_eq!(erased.downcast_ref::<usize>(), Some(&37));
}
