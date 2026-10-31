macro_rules! bind {
    ($p:pat, $value:expr, $result:expr) => {{ let $p = $value; $result }};
}
fn main() {
    assert_eq!(bind!(ref number, 17u32, *number), 17);
    assert_eq!(bind!(mut number, 19u32, { number += 2; number }), 21);
    assert_eq!(bind!(ref mut number, 23u32, { *number += 2; *number }), 25);
    let calls = std::cell::Cell::new(0);
    bind!(_, { calls.set(calls.get() + 1); 29 }, ());
    assert_eq!(calls.get(), 1);
}
