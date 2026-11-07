#[inline(never)]
fn build<T: Copy, const N: usize>(value: T, depth: u8) -> impl AsRef<[T]> + Copy {
    if depth == 0 {
        return [value; N];
    }
    build::<T, N>(value, depth - 1)
}

fn main() {
    let first = build::<u16, 3>(761, 2);
    let second = build::<u32, 2>(769, 1);
    assert_eq!(first.as_ref(), &[761u16; 3]);
    assert_eq!(second.as_ref(), &[769u32; 2]);
}
