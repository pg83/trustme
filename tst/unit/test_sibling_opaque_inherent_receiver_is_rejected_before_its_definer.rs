//@ compile-fail: No applicable methods
#![feature(type_alias_impl_trait)]

type Tokens = (impl Copy, impl Clone);
trait Parts { type Front; type Back; }
impl<A, B> Parts for (A, B) { type Front = A; type Back = B; }
type Front = <Tokens as Parts>::Front;
type Back = <Tokens as Parts>::Back;

struct Wrapped<T>(T);
impl Wrapped<Front> {
    fn code(&self) -> u8 { 53 }
}

fn a_check(value: Back) -> u8 { Wrapped(value).code() }

#[define_opaque(Tokens)]
fn z_tokens() -> Tokens { (59u8, 61u8) }

fn main() {
    let (_, back) = z_tokens();
    let _ = a_check(back);
}
