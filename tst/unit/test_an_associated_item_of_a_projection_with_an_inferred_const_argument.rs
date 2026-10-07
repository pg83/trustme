// tor-proto (arti) writes `<PollAll::<_, _> as Future>::Output::new()`, where
// `PollAll<'a, const N: usize, T>` resolves to `SmallVec<[T; N]>`. The path's
// item is looked up on the projection normalized with its `_`s standing for
// whatever inference finds; we did that for a `_` type but not for a `_`
// const argument, so the projection stayed unnormalized and `new` was "Failed
// to find impl". Upstream resolves the path during type checking with both as
// inference variables.
trait Collect {
    type Output;
}

struct Batch<'a, const N: usize, T>(&'a [T; N]);

impl<'a, const N: usize, T> Collect for Batch<'a, N, T> {
    type Output = Vec<[T; N]>;
}

fn make() -> Vec<[u8; 2]> {
    let mut out = <Batch::<_, _> as Collect>::Output::new();
    out.push([1u8, 2]);
    out
}

fn main() {
    let rows = make();
    assert_eq!(rows, vec![[1, 2]]);
    let _ = Batch(&[0u8; 2]).0;
}
