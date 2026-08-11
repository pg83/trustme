trait Step {
    fn replace_zero(&mut self) -> Self;
}

impl Step for i32 {
    fn replace_zero(&mut self) -> Self {
        std::mem::replace(self, 0)
    }
}

pub fn replace(value: &mut i32) -> i32 {
    value.replace_zero()
}
