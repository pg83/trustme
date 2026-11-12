struct Tail<T: ?Sized> {
    tag: u16,
    values: T,
}

trait Total {
    fn total(&self) -> u16;
}

impl Total for [u16; 3] {
    fn total(&self) -> u16 {
        self[0] + self[1] + self[2]
    }
}

fn main() {
    let storage = Tail {
        tag: 19u16,
        values: [23u16, 29u16, 31u16],
    };

    let slice_tail: &Tail<[u16]> = &storage;
    assert_eq!(slice_tail.tag, 19);
    assert_eq!(slice_tail.values.len(), 3);
    assert_eq!(slice_tail.values[1], 29);

    let trait_tail: &Tail<dyn Total> = &storage;
    assert_eq!(trait_tail.tag + trait_tail.values.total(), 102);
}
