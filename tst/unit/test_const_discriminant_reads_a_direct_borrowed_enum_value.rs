#![feature(core_intrinsics)]
#[repr(i16)]
enum State { Idle = -7, Ready = 13 }
const IDLE: i16 = std::intrinsics::discriminant_value(&State::Idle) as i16;
const READY: i16 = std::intrinsics::discriminant_value(&State::Ready) as i16;
fn main() { assert_eq!(IDLE, -7); assert_eq!(READY, 13); }
