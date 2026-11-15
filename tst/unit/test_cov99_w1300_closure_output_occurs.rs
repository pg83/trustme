//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Holder<F>(F);

impl<F, R> Holder<F>
where
    F: FnOnce() -> R,
{
    fn get(self) -> F::Output {
        (self.0)()
    }
}

fn main() {
    let number = 0;
    let _same_type = [number, Holder(|| number).get()];
}
