//@ crate-type: lib
//@ compile-flags: --emit=metadata

// A closure can be called through a generic parameter before the closure it
// stands for has been seen: what it captures is only known once the whole
// expression has been read.
fn call_through<F, G>(g: G, f: Box<F>) -> u32
where
    F: Fn() -> u32,
    G: Fn(Box<F>) -> u32,
{
    g(f)
}

pub fn plain() -> u32 {
    call_through(|f| (*f)(), Box::new(|| 5))
}

pub fn capturing(base: u32) -> u32 {
    call_through(|f| (*f)() + 1, Box::new(move || base))
}
