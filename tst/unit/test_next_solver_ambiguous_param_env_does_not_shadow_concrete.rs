//@ compile-flags: -Znext-solver=globally
//@ crate-type: lib

trait Select<Rhs> {
    type Output;

    fn select(self, rhs: Rhs) -> Self::Output;
}

struct Concrete;
struct Wrong;

impl Select<Concrete> for Concrete {
    type Output = u32;

    fn select(self, _: Concrete) -> u32 {
        1
    }
}

fn unrelated_param_env<I, U>()
where
    I: Iterator,
    I::Item: Select<U, Output = Wrong>,
{
    let _: u32 = Concrete.select(Concrete);
}
