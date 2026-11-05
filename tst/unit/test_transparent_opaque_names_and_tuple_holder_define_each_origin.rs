//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Pixel = impl Copy;
type Scale = impl Copy;
type PixelName = Pixel;
type ScaleName = Scale;

#[define_opaque(PixelName, ScaleName)]
fn coordinates() -> (Pixel, Scale) { (79u8, 83u16) }

type Pair = (impl Copy, impl Copy);
struct Holder(Pair);

#[define_opaque(Holder)]
fn holder() -> Holder { Holder((89u8, 97u16)) }

fn main() {
    let (pixel, scale) = coordinates();
    assert_eq!(std::mem::size_of_val(&pixel), 1);
    assert_eq!(std::mem::size_of_val(&scale), 2);
    let Holder(pair) = holder();
    assert_eq!(std::mem::size_of_val(&pair.0), 1);
    assert_eq!(std::mem::size_of_val(&pair.1), 2);
}
