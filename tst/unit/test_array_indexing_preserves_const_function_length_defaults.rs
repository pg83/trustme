const fn width() -> usize { 2 + 3 }
type Buffer<const N: usize = { width() }> = [u16; N];

fn read(value: &Buffer, index: usize) -> u16 { value[index] }

fn main() {
    let value: Buffer = [3, 5, 7, 11, 13];
    assert_eq!(read(&value, 0), 3);
    assert_eq!(read(&value, 4), 13);
}
