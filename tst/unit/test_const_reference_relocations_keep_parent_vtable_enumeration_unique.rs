#![feature(adt_const_params, unsized_const_params)]

trait Parent<const KEY: &'static [u8; 0]>: Sync {
    fn tag(&self) -> u8;
}
trait Child<const KEY: &'static [u8; 0]>: Parent<KEY> {}
struct Token;
impl<const KEY: &'static [u8; 0]> Parent<KEY> for Token {
    fn tag(&self) -> u8 { 29 }
}
impl<const KEY: &'static [u8; 0]> Child<KEY> for Token {}

struct Root {
    zero: [u8; 0],
    vtable: *const (),
}
unsafe impl Sync for Root {}
const KEY: &[u8; 0] = &ROOT.zero;
static ROOT: Root = Root {
    zero: [],
    vtable: unsafe {
        std::mem::transmute::<&dyn Child<KEY>, (*const (), *const ())>(&Token).1
    },
};

#[inline(never)]
fn read(value: &dyn Parent<KEY>) -> u8 { value.tag() }
fn main() {
    let parent: &dyn Parent<KEY> = &Token;
    assert_eq!(read(std::hint::black_box(parent)), 29);
}
