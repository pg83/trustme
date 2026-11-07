#![feature(generic_const_exprs)]
#![allow(incomplete_features, dead_code)]

struct Deferred<const N: usize> {
    bytes: [u8; {
        let callback = &|| {
            let word = 653u16;
            word + 2
        };
        let _ = callback;
        N
    }],
}

fn main() {
    let callback: &dyn Fn(u16) -> u16 = &|value| {
        let bias = 17;
        value + bias
    };
    assert_eq!(callback(631), 648);
}
