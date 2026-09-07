/* std's `StderrRaw::write_vectored`: `let total = || Ok(bufs.iter().map(|b| b.len()).sum());`
   leaves `sum::<S>()` open until `handle_ebadf`'s `F: FnOnce() -> Result<T>` bound
   says `S = usize`; upstream looked `sum` up at once and let the obligation wait. */
fn handle_ebadf<T>(r: Result<T, i32>, default: impl FnOnce() -> Result<T, i32>) -> Result<T, i32> {
    match r {
        Err(9) => default(),
        r => r,
    }
}

fn write_vectored(bufs: &[&[u8]], fail: bool) -> Result<usize, i32> {
    let total = || Ok(bufs.iter().map(|b| b.len()).sum());
    handle_ebadf(if fail { Err(9) } else { Ok(1) }, total)
}

fn main() {
    assert_eq!(write_vectored(&[b"ab", b"cde"], true), Ok(5));
    assert_eq!(write_vectored(&[b"ab"], false), Ok(1));
}
