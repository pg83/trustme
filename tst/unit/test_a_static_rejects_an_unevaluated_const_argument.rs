//@ compile-fail: error:0:

static VALUE: u8 = 7;

fn main() {
    let _ = VALUE::<{ 1usize + 2 }>;
}
