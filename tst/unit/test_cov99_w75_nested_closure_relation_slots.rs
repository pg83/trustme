//@ crate-type: lib

trait Project {
    type Output;
}

struct Wrap<T>(T);

impl<T, F, U> Project for Wrap<(T, F)>
where
    F: Fn(T) -> Option<U>,
{
    type Output = U;
}

trait Relation<R> {}

impl<L, R> Relation<R> for L where L: Project<Output = R> {}

fn require_relation<L: Relation<R>, R>(_: &L, _: &R) {}

pub fn nested_closure_relation() {
    let producer = Wrap(((), |()| Some(0)));
    require_relation(&producer, &0_i64);
}
