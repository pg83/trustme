trait WithDefaults<const N: u8 = 1, const M: u8 = N> {}

impl<const N: u8> WithDefaults<N> for u32 {}
impl WithDefaults for u8 {}

fn opaque<const N: u8>() -> impl WithDefaults<N> {
    0_u32
}

fn trait_object<const N: u8>(value: &dyn WithDefaults<N>) {
    let _ = value;
}

fn main() {
    opaque::<7>();
    trait_object(&0_u8);
}
