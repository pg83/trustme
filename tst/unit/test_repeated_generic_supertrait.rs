trait Compare<T> {
    fn same_as(&self, value: T) -> bool;
}

trait CompareBoth: Compare<i32> + Compare<u32> {}

impl Compare<i32> for i32 {
    fn same_as(&self, value: i32) -> bool {
        *self == value
    }
}

impl Compare<u32> for i32 {
    fn same_as(&self, value: u32) -> bool {
        *self == value as i32
    }
}

impl CompareBoth for i32 {}

fn through_object(value: &dyn CompareBoth) -> bool {
    value.same_as(7_i32) && value.same_as(7_u32)
}

fn through_ufcs<T: CompareBoth>(value: &T) -> bool {
    <dyn CompareBoth>::same_as(value, 7_i32)
        && <dyn CompareBoth>::same_as(value, 7_u32)
}

fn main() {
    assert!(through_object(&7));
    assert!(through_ufcs(&7));
}
