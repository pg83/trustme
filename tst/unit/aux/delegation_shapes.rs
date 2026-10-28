pub trait Value { fn value(&self) -> u8; }
impl Value for u8 { fn value(&self) -> u8 { *self } }

pub trait Shapes {
    fn unsigned(&self, value: (u8, u16, u32, u64, u128, usize)) -> (u8, u16, u32, u64, u128, usize) {
        value
    }
    fn signed(&self, value: (i8, i16, i32, i64, i128, isize)) -> (i8, i16, i32, i64, i128, isize) {
        value
    }
    fn scalars(&self, value: (f32, f64, bool, char)) -> (f32, f64, bool, char) {
        value
    }
    fn bytes(&self, value: &[u8]) -> usize { value.len() }
    fn text(&self, value: &str) -> usize { value.len() }
    fn change(&self, target: &mut u8, value: u8) { *target = value; }
    fn pointers(&self, source: *const u8, target: *mut u8, map: fn(u8) -> u8) {
        unsafe { *target = map(*source); }
    }
    fn object(&self, value: &(dyn Value + Send)) -> u8 { value.value() }
}

pub struct Inner;
impl Shapes for Inner {}
