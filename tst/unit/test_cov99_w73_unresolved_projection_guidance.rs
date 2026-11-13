//@ compile-fail: error:
trait Family { type Item; }
impl Family for u8 { type Item = u32; }
impl Family for u16 { type Item = u64; }

trait Make: Sized { fn make() -> Self; }
impl Make for u32 { fn make() -> Self { 32 } }
impl Make for u64 { fn make() -> Self { 64 } }

fn main() {
    let value = <_ as Make>::make();
    let _: <_ as Family>::Item = value;
}
