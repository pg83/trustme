const fn length<T: Copy>(_: T) -> usize {
    3
}

trait First {
    fn first(&self) -> u8;
}

impl First for [u8; length::<_>(7u8)] {
    fn first(&self) -> u8 {
        self[0]
    }
}

fn main() {
    assert_eq!(<[u8; 3] as First>::first(&[7, 9, 11]), 7);
}
