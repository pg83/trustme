//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: Too many type parameters passed to

trait Source {
    type Item;
}

fn inspect<T: Source>() {
    let _: Option<&dyn Send<<T as Source>::Item>> = None;
}

fn main() {}
