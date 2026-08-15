#![feature(type_alias_impl_trait)]

fn destructure<T>(value: T) {
    type Pair<T> = impl Sized;
    let pair: Pair<T> = (value,);
    let (_value,): (T,) = pair;
}

const fn destructure_const<T: Copy>(value: T) {
    type Pair<T: Copy> = impl Copy;
    let pair: Pair<T> = (value, 2u32);
    let (_value, _number): (T, u32) = pair;
}

fn main() {
    destructure(1u32);
    destructure_const(1u32);
    const VALUE: () = destructure_const(2u32);
    VALUE
}
