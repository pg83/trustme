//@ crate-type: lib

trait Project {
    type Output;
}
struct Wrap<F>(F);
impl<F, U> Project for Wrap<F>
where
    F: Fn() -> Option<U>,
{
    type Output = Option<U>;
}

struct Host<F>(F);
trait Take<A> {
    fn take(self, value: A) -> A;
}
impl<F> Take<fn() -> <Wrap<F> as Project>::Output> for Host<F>
where
    Wrap<F>: Project,
{
    fn take(self, value: fn() -> <Wrap<F> as Project>::Output)
        -> fn() -> <Wrap<F> as Project>::Output
    {
        value
    }
}

pub fn conditional_projection_coercion(choose: bool) {
    let callback = || Some(3);
    let selected = Host(callback).take(if choose { callback } else { callback });
    let _: Option<i32> = selected();
}
