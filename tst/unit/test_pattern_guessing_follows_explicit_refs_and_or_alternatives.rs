enum Choice { Left(u16), Right(u16) }

fn main() {
    let read = |&(Choice::Left(value) | Choice::Right(value))| value;
    assert_eq!(read(&Choice::Left(11)), 11);
    assert_eq!(read(&Choice::Right(17)), 17);
    let ignore: fn(&Choice) -> u16 = |_| 23;
    assert_eq!(ignore(&Choice::Left(29)), 23);
    let ((_, first), (second, ..)) = &((0u8, 31u16), (37u32, 41u64));
    assert_eq!((*first, *second), (31, 37));
}
