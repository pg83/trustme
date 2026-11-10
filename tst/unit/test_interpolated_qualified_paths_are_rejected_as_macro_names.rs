//@ compile-fail: No macro arm matched

macro_rules! invoke {
    ($path:path) => { $path!(); };
}

trait Route {
    type Item;
}

invoke!(<u8 as Route>::Item);

fn main() {}
