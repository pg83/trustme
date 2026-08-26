// Regression: canonical const value slots (reserved-range value-infer
// indexes) flow into legacy comparisons and nested goals during candidate
// assembly; resolving them by raw table index aborted on the bounds assert.
// Reduced from rust 1.90 ui test const-generics/const-param-in-async.rs.

async fn foo<const N: usize>(arg: [u8; N]) -> usize {
    arg.len()
}

async fn bar<const N: usize>() -> [u8; N] {
    [0; N]
}

trait Trait<const N: usize> {
    fn fynn(&self) -> usize;
}

impl<const N: usize> Trait<N> for [u8; N] {
    fn fynn(&self) -> usize {
        N
    }
}

async fn baz<const N: usize>() -> impl Trait<N> {
    [0; N]
}

async fn biz<const N: usize>(v: impl Trait<N>) -> usize {
    v.fynn()
}

async fn user<const N: usize>() -> usize {
    foo::<N>(bar().await).await + biz(baz::<N>().await).await
}

fn main() {
    let _ = user::<3>;
}
