struct Slot<T, const N: usize>([T; N]);

trait Last {
    type Item;
    fn last(self) -> Self::Item;
}

impl<T: Copy, const N: usize> Last for Slot<T, N> {
    type Item = T;

    fn last(self) -> T {
        self.0[N - 1]
    }
}

fn main() {
    assert_eq!(Slot([137u16; 3]).last(), 137);
    assert_eq!(Slot([139u32; 3]).last(), 139);
    assert_eq!(Slot([149u16; 5]).last(), 149);
    assert_ne!(
        std::any::TypeId::of::<Slot<u16, 3>>(),
        std::any::TypeId::of::<Slot<u16, 5>>(),
    );
    assert_ne!(
        std::any::TypeId::of::<Slot<u16, 3>>(),
        std::any::TypeId::of::<Slot<u32, 3>>(),
    );
}
