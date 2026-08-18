// A generic parameter of an item may shadow nothing from the item around it;
// what this checks is that the ones that do not shadow still work.
trait SomeTrait<'a, T, const N: usize> {
    fn no_clash<U, const M: usize>(&self, _: &'a U) -> usize {
        N + M
    }
}

struct S;

impl<'a, T, const N: usize> SomeTrait<'a, T, N> for S {}

// A `for<..>` binder is its own scope, so a lifetime there may repeat one from
// another binder.
fn hrb(f: &dyn for<'b> Fn(&'b u8), g: &dyn for<'b> Fn(&'b u16)) {
    f(&1);
    g(&2);
}

fn main() {
    let s = S;
    assert_eq!(SomeTrait::<'static, u8, 2>::no_clash::<u16, 3>(&s, &0u16), 5);
    hrb(&|_| {}, &|_| {});
}
