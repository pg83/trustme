//@ compile-fail: Type mismatch
struct Sensor;
impl Sensor {
    fn address(&self) -> *const u32 { core::ptr::null() }
}
fn main() {
    let _: *const u8 = Sensor.address();
}
