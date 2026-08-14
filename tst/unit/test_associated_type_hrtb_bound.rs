trait Source {
    type Item;
}

fn require<T: Source<Item: for<'a> Into<&'a u8>>>() {}

fn main() {}
