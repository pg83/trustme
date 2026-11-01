type Buffer<const N: usize> = [u16; N];

fn read<const N: usize>(value: &Buffer<N>, index: usize) -> u16 {
    value[index]
}

fn main() {
    let first: Buffer<3> = [5, 7, 11];
    let second: Buffer<5> = [13, 17, 19, 23, 29];
    assert_eq!(read(&first, 0), 5);
    assert_eq!(read(&first, 2), 11);
    assert_eq!(read(&second, 1), 17);
    assert_eq!(read(&second, 4), 29);
}
