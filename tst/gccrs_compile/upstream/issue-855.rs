pub fn test(value: i32) -> Result<i32, bool> {
    if value > 5 {
        Ok(123)
    } else {
        Err(false)
    }
}
