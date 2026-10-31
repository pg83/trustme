fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn constant<T: Copy>(value: T) -> impl Fn() -> T { move || value }
fn main() {
    let mut closure = constant(13u16);
    replace(&mut closure, constant(17u16));
    assert_eq!(closure(), 17);
    let mut function: fn(u16) -> u32 = |x| x as u32;
    replace(&mut function, |x| x as u32 + 1);
    assert_eq!(function(19), 20);
}
