const I8_MIN_F32: f32 = i8::MIN as f32;
const I16_MIN_F32: f32 = i16::MIN as f32;
const I32_MIN_F32: f32 = i32::MIN as f32;
const I64_MIN_F32: f32 = i64::MIN as f32;
const ISIZE_MIN_F64: f64 = isize::MIN as f64;

fn to_i64(n: f32) -> Option<i64> {
    const MIN: f32 = i64::MIN as f32;
    const MAX_P1: f32 = i64::MAX as f32;
    if n >= MIN && n < MAX_P1 {
        Some(unsafe { n.to_int_unchecked::<i64>() })
    } else {
        None
    }
}

trait FloatCore {
    fn trunc(self) -> Self;
    fn integer_decode(self) -> (u64, i16, i8);
}

impl FloatCore for f64 {
    fn trunc(self) -> Self {
        f64::trunc(self)
    }

    fn integer_decode(self) -> (u64, i16, i8) {
        let bits = self.to_bits();
        let sign = if bits >> 63 == 0 { 1 } else { -1 };
        (bits & 0xfffffffffffff, 0, sign)
    }
}

trait FromPrimitive: Sized {
    fn from_f32(n: f32) -> Option<Self> {
        Self::from_f64(f64::from(n))
    }

    fn from_f64(n: f64) -> Option<Self>;
}

struct NonNegative;

impl FromPrimitive for NonNegative {
    fn from_f64(mut n: f64) -> Option<Self> {
        n = FloatCore::trunc(n);
        if n == 0.0 {
            return Some(NonNegative);
        }
        let (_, _, sign) = FloatCore::integer_decode(n);
        if sign == -1 {
            None
        } else {
            Some(NonNegative)
        }
    }
}

fn main() {
    assert_eq!(I8_MIN_F32, -128.0);
    assert_eq!(I16_MIN_F32, -32768.0);
    assert_eq!(I32_MIN_F32, -2147483648.0);
    assert_eq!(I64_MIN_F32, -9223372036854775808.0);
    assert_eq!(ISIZE_MIN_F64, -9223372036854775808.0);
    assert_eq!(to_i64(-1.0), Some(-1));
    let widened = f64::from(-1.0f32);
    assert_eq!(widened, -1.0);
    assert_eq!(widened.to_bits(), (-1.0f64).to_bits());
    assert!(NonNegative::from_f32(-1.0).is_none());
}
