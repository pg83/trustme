trait Repack<T, const N: usize> {
    type Value;
    fn repack(value: [T; N]) -> Self::Value;
}

struct Relay;

impl<T: Copy, const N: usize> Repack<T, N> for Relay {
    type Value = [T; N];

    fn repack(value: [T; N]) -> Self::Value {
        value
    }
}

fn through<T: Copy, const N: usize>(value: [T; N]) -> [T; N]
where
    Relay: Repack<T, N, Value = [T; N]>,
{
    <Relay as Repack<T, N>>::repack(value)
}

fn main() {
    assert_eq!(through([17u16, 19, 23]), [17, 19, 23]);
    assert_eq!(through([197u8]), [197]);
    assert_eq!(through::<u32, 0>([]), []);
}
