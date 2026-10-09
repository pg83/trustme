// `y = uv.min(y); uv = y;` and then `y.ilog()` on an integer literal variable,
// later passed to a `usize` parameter, with a blanket `ILog` trait in scope.
// rustc's `check_expr_assign` checks the right-hand side first and coerces it
// into the place afterwards, so at `.ilog()` both variables are `{integer}`,
// the probe finds no inherent impl for an integer variable and picks
// `ILog::ilog`. Our receiver pause placed an assignment's coercion at the
// start of the assignment, before the call on its right-hand side, so the
// call waited for the coercion of its own result. The pauses held each
// other until a later argument made the variable `usize`, and the inherent
// `usize::ilog(self, base)` was picked: rav1e's `RestorationState::new`.
trait Bits: Copy {
    const BITS_USED: usize;
    fn zeros(self) -> u32;
}

impl Bits for usize {
    const BITS_USED: usize = usize::BITS as usize;
    fn zeros(self) -> u32 {
        self.leading_zeros()
    }
}

impl Bits for i32 {
    const BITS_USED: usize = i32::BITS as usize;
    fn zeros(self) -> u32 {
        self.leading_zeros()
    }
}

trait ILog: Bits {
    fn ilog(self) -> usize {
        Self::BITS_USED - self.zeros() as usize
    }
}

impl<T: Bits> ILog for T {}

struct Plane {
    unit_size: usize,
    unit_log2: usize,
}

impl Plane {
    fn new(unit_size: usize, unit_log2: usize) -> Self {
        Plane { unit_size, unit_log2 }
    }
}

fn planes(width: usize, shift: usize, tile_width: usize, ydec: usize) -> (Plane, Plane, usize) {
    let y_sb_log2 = if width > 1000 { 7 } else { 6 };
    let mut y_unit_size = 1 << (8 - shift);
    let mut uv_unit_size = 1 << (8 - shift - ydec);
    if tile_width > 1 {
        let trailing = tile_width.trailing_zeros() as usize;
        let aligned_y = 1 << (y_sb_log2 + trailing);
        let aligned_uv_h = 1 << (y_sb_log2 - 1 + trailing);
        let aligned_uv_v = 1 << (y_sb_log2 - ydec + trailing);
        y_unit_size = y_unit_size.min(aligned_y);
        uv_unit_size = uv_unit_size.min(aligned_uv_h.min(aligned_uv_v));
    }
    if ydec == 0 && y_unit_size != uv_unit_size {
        y_unit_size = uv_unit_size.min(y_unit_size);
        uv_unit_size = y_unit_size;
    }
    let y_unit_log2 = y_unit_size.ilog() - 1;
    let uv_unit_log2 = uv_unit_size.ilog() - 1;
    let y_cols = ((width + (y_unit_size >> 1)) / y_unit_size).max(1);
    (Plane::new(y_unit_size, y_unit_log2 - y_sb_log2), Plane::new(uv_unit_size, uv_unit_log2), y_cols)
}

fn main() {
    let (y, uv, cols) = planes(640, 2, 1, 0);
    assert_eq!((y.unit_size, y.unit_log2, uv.unit_size, uv.unit_log2, cols), (64, 0, 64, 6, 10));
    let (y, uv, cols) = planes(1920, 1, 4, 1);
    assert_eq!((y.unit_size, y.unit_log2, uv.unit_size, uv.unit_log2, cols), (128, 0, 64, 6, 15));
}
