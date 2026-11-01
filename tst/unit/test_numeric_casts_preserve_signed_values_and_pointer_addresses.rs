#[inline(never)]
fn signed(value: i64) -> (f32, f64, u8, i16) {
    (value as f32, value as f64, value as u8, value as i16)
}

#[inline(never)]
fn byte(value: u8) -> (char, f32, f64) {
    (value as char, value as f32, value as f64)
}

#[inline(never)]
fn floats(value: f64) -> (i32, u32, f32) {
    (value as i32, value as u32, value as f32)
}

#[repr(u16)]
enum Code { Ready = 513 }

fn increment(x: u16) -> u16 { x + 1 }

fn main() {
    assert_eq!(signed(-257), (-257.0, -257.0, 255, -257));
    assert_eq!(byte(233), ('é', 233.0, 233.0));
    assert_eq!(floats(-3.75), (-3, 0, -3.75));
    assert_eq!(Code::Ready as u16, 513);
    let value = 17u16;
    let ptr = &value as *const u16;
    let address = ptr as usize;
    assert_eq!(unsafe { *(address as *const u16) }, 17);
    assert_eq!(ptr as *const u8 as usize, address);
    let function = increment as fn(u16) -> u16;
    let unsafe_function = function as unsafe fn(u16) -> u16;
    assert_eq!(unsafe { unsafe_function(5) }, 6);
    assert_eq!(increment as usize, function as usize);
    assert_eq!(function as *const () as usize, function as usize);
}
