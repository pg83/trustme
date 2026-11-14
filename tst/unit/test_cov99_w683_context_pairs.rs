//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![allow(dead_code)]

trait Value {
    type Output: Copy;
}

impl Value for u8 {
    type Output = u8;
}

impl Value for u16 {
    type Output = u16;
}

struct Need<T: Value>(T);

macro_rules! family {
    ($name:ident; $($method:ident),+) => {
        struct $name<T: Value>(T);
        impl<T: Value> $name<T> {
            $(fn $method<U: Value>(&self, x: Need<T>, y: Need<U>) -> (Need<T>, Need<U>) {
                (x, y)
            })+
        }
    };
}

family!(A; a, b, c, d, e, f, g, h);
family!(B; a, b, c, d, e, f, g);
family!(C; a, b, c, d, e, f);
family!(D; a, b, c, d, e);
family!(E; a, b, c, d);
family!(F; a, b, c);
family!(G; a, b);
family!(H; a);

pub fn sample() -> (u8, u16) {
    let (x, y) = A(0u8).a(Need(3u8), Need(4u16));
    (x.0, y.0)
}
