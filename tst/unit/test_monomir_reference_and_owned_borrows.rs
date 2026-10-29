//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=2 -Zdump-mir
//@ output-contains: .mir = &move (*arg0
//@ output-contains: .mir = &mut arg0.0;
//@ output-contains: .mir = &arg0.1;
#[inline(never)]
pub fn read(value: &u32) -> u32 { *value }
#[inline(never)]
pub fn replace(value: &mut u32, next: u32) { *value = next; }
pub fn process(mut pair: (u32, u32)) -> u32 {
    replace(&mut pair.0, 7);
    read(&pair.1) + pair.0
}
pub fn consume(callback: Box<dyn FnOnce(u32) -> u32>, value: u32) -> u32 {
    callback(value)
}
pub fn boxed(value: u32) -> u32 {
    let captured = vec![3u32, 5];
    consume(Box::new(move |input| captured[0] + captured[1] + input), value)
}
