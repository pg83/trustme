//@ compile-fail: Couldn't find pattern type 'Self'
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Marker;

impl Marker {
    fn check() {
        fn nested() {
            let _ = Self {};
        }
    }
}

fn main() {}
