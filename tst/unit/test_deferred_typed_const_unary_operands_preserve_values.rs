//@ compile-flags: -Coverflow-checks=off
fn invert<const N: u16>() -> u16 { !N }
fn flag<const N: bool>() -> bool { !N }
fn negate<const N: i128>() -> i128 { -N }
fn main() {
    assert_eq!((invert::<37>(), invert::<65535>()), (65498, 0));
    assert_eq!((flag::<true>(), flag::<false>()), (false, true));
    assert_eq!((negate::<37>(), negate::<-41>()), (-37, 41));
}
