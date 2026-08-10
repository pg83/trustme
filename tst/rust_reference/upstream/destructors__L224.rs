// Extracted from src/destructors.md:224
#![allow(unused)]
fn main() {
    struct PrintOnDrop(&'static str);
    impl Drop for PrintOnDrop {
        fn drop(&mut self) {
            println!("drop({})", self.0);
        }
    }
    // Drops `x` before `y`.
    fn or_pattern_drop_order<T>(
        (Ok([x, y]) | Err([y, x])): Result<[T; 2], [T; 2]>
    //   ^^^^^^^^^^   ^^^^^^^^^^^ This is the second subpattern.
    //   |
    //   This is the first subpattern.
    //
    //   In the first subpattern, `x` is declared before `y`. Since it is
    //   the first subpattern, that is the order used even if the second
    //   subpattern, where the bindings are declared in the opposite
    //   order, is matched.
    ) {}
    
    // Here we match the first subpattern, and the drops happen according
    // to the declaration order in the first subpattern.
    or_pattern_drop_order(Ok([
        PrintOnDrop("Declared first, dropped last"),
        PrintOnDrop("Declared last, dropped first"),
    ]));
    
    // Here we match the second subpattern, and the drops still happen
    // according to the declaration order in the first subpattern.
    or_pattern_drop_order(Err([
        PrintOnDrop("Declared last, dropped first"),
        PrintOnDrop("Declared first, dropped last"),
    ]));
}
