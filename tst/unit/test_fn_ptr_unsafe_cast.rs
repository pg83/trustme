// A safe function pointer casts and coerces to an unsafe one: the two types
// differ in unsafety, and only the signature has to agree.
fn takes_nothing() {}

fn doubles(value: u32) -> u32 {
    value * 2
}

const fn reify(f: fn()) -> unsafe fn() {
    f
}

const fn reify_named() -> unsafe fn() {
    takes_nothing as unsafe fn()
}

fn main() {
    let cast = takes_nothing as fn() as unsafe fn();
    unsafe { cast() };

    let coerced: unsafe fn() = takes_nothing;
    unsafe { coerced() };

    unsafe { reify(takes_nothing)() };
    unsafe { reify_named()() };

    let with_args = doubles as fn(u32) -> u32 as unsafe fn(u32) -> u32;
    assert_eq!(unsafe { with_args(21) }, 42);
}
