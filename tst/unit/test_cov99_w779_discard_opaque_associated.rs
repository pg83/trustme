trait Family {
    type Item;
}

struct ByteFamily;

impl Family for ByteFamily {
    type Item = u8;
}

fn discard<F: Family>(value: F::Item) {
    value;
}

fn main() {
    discard::<ByteFamily>(7u8);
}
