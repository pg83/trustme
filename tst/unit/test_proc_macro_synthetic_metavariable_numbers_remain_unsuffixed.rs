//@ proc-macro-aux-build: numeric_token_bridge.rs
#![feature(macro_metavar_expr)]
use numeric_token_bridge::echo;
macro_rules! count {
    ($($name:ident),*) => { echo!(${count($name)}) };
}
macro_rules! indices {
    ($($name:ident),*) => { echo!([$( { ${ignore($name)} ${index()} } ),*]) };
}
fn main() {
    let small: u8 = count!(red, green, blue);
    let wide: u128 = count!(one, two, three, four);
    assert_eq!((small, wide), (3u8, 4u128));
    let positions: [u16; 4] = indices!(a, b, c, d);
    assert_eq!(positions, [0, 1, 2, 3]);
}
