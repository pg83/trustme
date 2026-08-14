trait Source {
    type Item;
}

fn require_static<T: Source<Item: 'static>>() {}

fn main() {}
