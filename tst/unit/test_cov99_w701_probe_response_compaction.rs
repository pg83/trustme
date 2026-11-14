//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait PairOutput {
    type Output;
    fn output(self) -> Self::Output;
}

struct Pair<I, J>(I, J);

impl<I: Iterator, J: Iterator> PairOutput for Pair<I, J> {
    type Output = (I::Item, J::Item);

    fn output(self) -> Self::Output {
        loop {}
    }
}

fn main() {
    let _: (u8, Option<_>) = Pair(
        std::iter::once(())
            .filter_map(|_| Default::default())
            .filter(|_| true),
        std::iter::once(Some(7u16)),
    )
    .output();
}
