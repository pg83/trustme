//@ compile-only
//@ compile-flags: -Zunpretty=hir-tree

impl<T> T {
    fn read(&self) -> u8 {
        37
    }
}

fn main() {}
